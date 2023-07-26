#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <netinet/tcp.h>

#include <signal.h>
#include <setjmp.h>

#include "caveat.h"
#include "hart.h"

extern option<bool> conf_show;

//#define msg(fmt, ...)                  { fprintf(stderr, fmt, ##__VA_ARGS__); fprintf(stderr, "\n"); }
#define msg(fmt, ...)

uintptr_t gdb_text, gdb_data, gdb_bss;

//	E01 - Command syntax error.
//	E02 - Error in hex data.


#define NUMREGS   32		/* General purpose registers per context */

static int lastGdbSignal = 0;
static hart_t* gdb_cpu;
#ifdef SPIKE
static reg_t* gdb_pc;
static reg_t* gdb_reg;
#else
static uintptr_t* gdb_pc;
static long* gdb_reg;
#endif
//long gdbNumContinue = -1;	/* program started by 'c' */
long gdbNumContinue = 0;	/* program started by 'c' */

#define INBUFSIZE    ((NUMREGS+1)*16 + 100)
#define OUTBUFSIZE   ((NUMREGS+1)*16 + 100)

static char inBuf[INBUFSIZE];
static char outBuf[OUTBUFSIZE];

static char* inPtr = inBuf;
static char* outPtr = outBuf;


static sigjmp_buf bombed;

static void
LocalExceptionHandler(int signum) {
  msg("***** Caught exception %d\n", signum);
  siglongjmp(bombed, signum);
}

static struct sigaction localAction = { &LocalExceptionHandler, };

bool valid_memory(void* address, int bytes, bool store =false)
{
  struct sigaction sigsegv_buf, sigbus_buf;
  // new actions
  sigaction(SIGSEGV, &localAction, &sigsegv_buf);
  sigaction(SIGBUS,  &localAction, &sigbus_buf);
  // prepare to catch exception
  int signum;
  if (signum = sigsetjmp(bombed, 1)) {
    const char* signame = 0;
    switch (signum) {
    case SIGTRAP:	signame = "SIGTRAP";	break;
    case SIGBUS:	signame = "SIGBUS";	break;
    case SIGSEGV:	signame = "SIGSEGV";	break;
    case SIGFPE:	signame = "SIGFPE";	break;
    default:
      msg("Caught exception %d\n", signum);
    }
    msg("Caught exception %s\n", signame);
    sigaction(SIGSEGV, &sigsegv_buf, 0);
    sigaction(SIGBUS,  &sigbus_buf,  0);
    return false;
  }
  // probe for exception
  volatile char* p = (char*)address;
  if (store) {
    for (int k=0; k<bytes; k++)
      *p++ = 0;
  }
  else {
    int sum = 0;
    for (int k=0; k<bytes; k++)
      sum += *p++;
  }
  // all okey
  sigaction(SIGSEGV, &sigsegv_buf, 0);
  sigaction(SIGBUS,  &sigbus_buf,  0);
  return true;
}


static int tcpLink;

static int
getDebugChar() {
  char buf;
  int rc;
  do {
    rc = read(tcpLink, &buf, 1);
    if (rc < 0) {
      perror("getDebugChar");
      abort();
    }
  } while (rc < 1);
  return buf;
}

static void
putDebugChar(int ch) {
  int rc;
  do {
    rc = write(tcpLink, &ch, 1);
    if (rc < 0) {
      perror("putDebugChar");
      abort();
    }
  } while (rc < 1);
}


static void
OpenTcpLink(const char* name) {
  const char *port_str;
  int port;
  struct sockaddr_in sockaddr;
  //  int tmp;
  unsigned tmp;
  struct protoent *protoent;
  int tmp_desc;

  port_str = strchr (name, ':');

  port = atoi (port_str + 1);

  tmp_desc = socket (PF_INET, SOCK_STREAM, 0);
  dieif(tmp_desc<0, "Can't open socket");

      /* Allow rapid reuse of this port. */
  tmp = 1;
  setsockopt (tmp_desc, SOL_SOCKET, SO_REUSEADDR, (char *) &tmp,
	      sizeof (tmp));

  sockaddr.sin_family = PF_INET;
  sockaddr.sin_port = htons (port);
  sockaddr.sin_addr.s_addr = INADDR_ANY;

  dieif(bind (tmp_desc, (struct sockaddr *) &sockaddr, sizeof (sockaddr)) || listen (tmp_desc, 1), "Can't bind address");

  tmp = sizeof (sockaddr);
  tcpLink = accept (tmp_desc, (struct sockaddr *) &sockaddr, &tmp);
  dieif(tcpLink == -1, "Accept failed");

  protoent = getprotobyname ("tcp");
  dieif(!protoent, "getprotobyname");

      /* Enable TCP keep alive process. */
  tmp = 1;
  setsockopt (tmp_desc, SOL_SOCKET, SO_KEEPALIVE, (char *) &tmp, sizeof (tmp));

      /* Tell TCP not to delay small packets.  This greatly speeds up
         interactive response. */
  tmp = 1;
  setsockopt (tcpLink, protoent->p_proto, TCP_NODELAY,
	      (char *) &tmp, sizeof (tmp));

  close (tmp_desc);		/* No longer need this */
}


static int
val2hexch(int val) {
  val &= 0xF;
  if (0 <= val && val <= 9)
    return val + '0';
  else
    return val - 10 + 'a';
}

static int
hexch2val(int ch) {
  if ('0' <= ch && ch <= '9')
    return ch - '0';
  else if ('A' <= ch && ch <= 'F')
    return ch - 'A' + 10;
  else if ('a' <= ch && ch <= 'f')
    return ch - 'a' + 10;
  else
    return -1;			// Error signal.
}

static void
ReceivePacket() {
  unsigned char checksum;
  unsigned char xmitcsum;
  char* p;
  int ch;
  //
  // Wait around for start character, ignoring all other characters.
  //
 again:
  do {
    ch = getDebugChar();
  } while (ch != '$');
  //
  // Read until end character ('#'), but don't overflow buffer.
  //
 retry:
  checksum = 0;
  p = inBuf;
  ch = getDebugChar();
  while (ch != '#') {
    if (p >= inBuf + INBUFSIZE-1)
      goto finish;		// Buffer overflow...
    if (ch == '$')
      goto retry;
    checksum += ch;
    *p++ = ch;
    ch = getDebugChar();
  }

  *p = '\0';
  msg("ReceivePacket: `%s'...", inBuf);
  //
  // Now read checksum and compare.
  //
  ch = getDebugChar();
  xmitcsum = hexch2val(ch) << 4;
  ch = getDebugChar();
  xmitcsum |= hexch2val(ch);
  if (checksum != xmitcsum) {
    putDebugChar('-');		// Bad checksum indicator.
    goto again;
  }
  else
    putDebugChar('+');		// Good checksum indicator.
  //
  // Append terminator to string and reset read pointer.
  //
 finish:
  *p = '\0';
  inPtr = inBuf;

  //  msg("OK\n");
}

static void
SendPacket() {
  msg("SendPacket: `%s'\n", outBuf);
  do {
    unsigned char checksum = 0;
    char* p = outBuf;
    unsigned char ch;
    //
    // Send `$<packet info>#<checksum>' to gdb.
    //
    putDebugChar('$');
    while (ch = *p++) {
      checksum += ch;
      putDebugChar(ch);
    }
    putDebugChar('#');
    putDebugChar(val2hexch(checksum >> 4));
    putDebugChar(val2hexch(checksum & 0xF));
  } while (getDebugChar() != '+');
  outPtr = outBuf;
  *outPtr = '\0';
}

static void
Reply(const char* msg) {
  while (*msg)
    *outPtr++ = *msg++;
  *outPtr = '\0';
}

static void
ReplyInHex(void* address, int bytes) {
  if (valid_memory(address, bytes)) {
    char* p = (char*)address;
    while (bytes-- > 0) {
      *outPtr++ = val2hexch(*p >> 4);
      *outPtr++ = val2hexch(*p++ & 0xF);
    }
    *outPtr = '\0';
  }
  else
    Reply("E09");
}

static void
ReplyInt(long long unsigned v, int bytes) {
  msg("ReplyInt(v=0x%llx, bytes=%d)\n", v, bytes);
  int hexdigits = 2 * bytes;
  if (outPtr + hexdigits > outBuf + OUTBUFSIZE-1)
    abort();			// Internal error: output buffer overflow.
  while (hexdigits-- > 0) {
    *outPtr++ = val2hexch((v >> 4*hexdigits) & 0xF);
  }
  *outPtr = '\0';
}

static int
RcvWord(const char* match) {
  if (strncmp(inPtr, match, strlen(match)) == 0)
    return 1;
  else
    return 0;
}

static int
RcvHexDigit(int* value) {
  int digit;
  if ('0' <= *inPtr && *inPtr <= '9')
    digit = *inPtr++ - '0';
  else if ('A' <= *inPtr && *inPtr <= 'F')
    digit = *inPtr++ - 'A' + 10;
  else if ('a' <= *inPtr && *inPtr <= 'f')
    digit = *inPtr++ - 'a' + 10;
  else
    return 0;
  *value = digit;
  return 1;
}

static int
RcvHexInt(long* ptr) {
  int value;
  int digit;
  if (!RcvHexDigit(&digit))
    return 0;			/* Must have at least 1 digit. */
  value = digit;
  while (RcvHexDigit(&digit)) {
    value <<= 4;
    value |= digit;
  }
  *ptr = value;
  return 1;
}

static int
RcvHexToMemory(void* address, int bytes) {
  if (valid_memory(address, bytes, true)) {
    unsigned char* p = (unsigned char*)address;
    while (bytes-- > 0) {
      int value;
      int digit;
      if (!RcvHexDigit(&digit))	/* First of two hex digit */
	return 0;
      value = digit << 4;
      if (!RcvHexDigit(&digit))	/* Second hex digit in byte. */
	return 0;
      value |= digit;
      *p++ = value;
    }
    return (*inPtr == '\0');	// In case of extraneous stuff.
  }
  else
    return 0;
}


static void
ProcessGdbCommand() {
  for (;;) {
    int errors = 0;
    ReceivePacket();		// Resets inPtr to beginning of inBuffer.
    switch (*inPtr++) {
    case 'g':			// Send all CPU register values to gdb.
      msg("GDB_COMMAND: g\n");
      ReplyInHex((void*)gdb_reg, NUMREGS*8);
      ReplyInHex((void*)gdb_pc,  8);
      break;
    case 'G':			// gdb sets all CPU register values.
      msg("GDB_COMMAND: G\n");
      errors += RcvHexToMemory((void*)gdb_reg, NUMREGS*8);
      errors += RcvHexToMemory((void*)gdb_pc,  8);
      if (errors)
	Reply("OK");
      else
	Reply("E02");
      break;
    case '?':			// gdb asks what was last signal.
      msg("GDB_COMMAND: ?\n");
      Reply("S");
      ReplyInt(lastGdbSignal, 1);
      break;
    case 'P':			// Prr=VVVV - gdb sets single CPU register rr.
      {
        msg("GDB_COMMAND: P\n");
	long regno;
	if (RcvHexInt(&regno) && *inPtr++ == '=' && 0 <= regno && regno < NUMREGS) {
	  if (RcvHexToMemory((void*)&gdb_reg[regno], 8))
	    Reply("OK");
	  else
	    Reply("E02");
	}
	else
	  Reply("E01");
      }
      break;
    case 'm':			// mAAAA,LLL - send LLL bytes at AAAA to gdb.
      {
        msg("GDB_COMMAND: m\n");
	long addr;		// Beginning address.
	long length;		// Number of bytes.
	if (RcvHexInt(&addr) && *inPtr++ == ',' && RcvHexInt(&length)) {
	  ReplyInHex((char*)addr, length);
	}
	else
	  Reply("E01");
      }
      break;
    case 'M':			// Gdb writes LLL bytes at AA..AA.
      {
        msg("GDB_COMMAND: M\n");
	long addr;		// Beginning address.
	long length;		// Number of bytes.
	if (RcvHexInt(&addr) && *inPtr++ == ',' && RcvHexInt(&length) && *inPtr++ == ':') {
	  if (RcvHexToMemory((char*)addr, length)) {
	    Reply("OK");
	  }
	  else
	    Reply("E02");
	}
	else
	  Reply("E01");
      }
      break;
    case 's':			// Single step.
      {
        msg("GDB_COMMAND: s\n");
	long addr;
	if (*inPtr == '\0')
	  return;		// Continue at current pc.
	else if (RcvHexInt(&addr)) {
	  *gdb_pc = addr;
	  return;		// Continue at specified pc.
	}
	else
	  Reply("E01");
      }
      break;

    case 'c':			// cAA..AA - continue at address AA..AA
      msg("GDB_COMMAND: c\n");
      {
	//	++gdbNumContinue;
	gdbNumContinue = 1;
	long addr;
	if (*inPtr == '\0')
	  return;		// Continue at current pc.
	else if (RcvHexInt(&addr)) {
	  *gdb_pc = addr;
	  return;		// Continue at specified pc.
	}
	else
	  Reply("E01");
      }
      break;
      
#if 0
    case 'q':
      msg("gdb command: q");
      if (RcvWord("Offsets")) {
	char buf[1024];
	sprintf(buf, "TextSeg=%lx;DataSeg=%lx", gdb_text, gdb_data);
	//        sprintf(buf, "TextSeg=%lx", gdb_text);
	Reply(buf);
      }
      else if (RcvWord("Symbol::")) {
	Reply("OK");
      }
#if 0
      else if (RcvWord("L1")) {
	char buf[1024];
        sprintf(buf, "qM010%016lx", gdb_cpu->tid());
	Reply(buf);
      }
      else if (RcvWord("L020")) {
	long addr;
	char buf[1024];
	RcvHexInt(&addr);
        sprintf(buf, "qM010%016lx", addr+gdb_text);
	Reply(buf);
      }
#endif
      break;
#endif

    } /* switch */
    SendPacket();		// Resets outPtr to beginning of outBuffer.
  } /* for (;;) */
}


static sigjmp_buf mainGdbJmpBuf;

static void signal_handler(int nSIGnum)
{
  lastGdbSignal = nSIGnum;
  siglongjmp(mainGdbJmpBuf, 1);
}

static struct sigaction mainGdbAction = { signal_handler, };

static void ProcessGdbException()
{
  msg("ProcessGdbException, lastGdbSignal=%d\n", lastGdbSignal);
  Reply("T");
  ReplyInt(lastGdbSignal, 1);	// signal number
  Reply("20:");			// PC is register #32
  ReplyInHex((void*)gdb_pc, 8);
  Reply(";");
  SendPacket();			// Resets outPtr.
  lastGdbSignal = 0;
}


void controlled_by_gdb(const char* host_port, hart_t* cpu)
{
  abort();

#if 0
  gdb_cpu = cpu;
#ifdef SPIKE
  gdb_pc = &cpu->pc;
  gdb_reg = cpu->strand->s.spike_cpu.get_state()->XPR;
#else
  gdb_pc = &cpu->pc;
  gdb_reg = (long*)cpu->s.xrf;
#endif

  msg("Opening TCP link to GDB\n");
  OpenTcpLink(host_port);
  //  signal(SIGABRT, signal_handler);
  //  signal(SIGFPE,  signal_handler);
  //  signal(SIGSEGV, signal_handler);
  //    signal(SIGILL,  signal_handler);
  //    signal(SIGINT,  signal_handler);
  //    signal(SIGTERM, signal_handler);
  msg("Waiting on GDB\n");
  while (1) {
    msg("ProcessGdbCommand() top of while loop\n");
    ProcessGdbCommand();
    msg("  Returned from ProcessGdbCommand()");
#if 1
    struct sigaction sigsegv_buf, sigbus_buf;
    sigaction(SIGSEGV, &mainGdbAction, &sigsegv_buf);
    sigaction(SIGBUS,  &mainGdbAction, &sigbus_buf);
    if (sigsetjmp(mainGdbJmpBuf, 1)) {
      msg("ProcessGdbException() in exception\n");
      ProcessGdbException();
      goto cleanup;
    }
#endif
    //    signal(SIGTRAP, signal_handler);
    do {
      //      if (conf_show) {
      //	Insn_t i = decoder(*gdb_pc);
      //	labelpc(*gdb_pc);
      //	disasm(*gdb_pc, &i);
      //      }
    } while (!cpu->single_step());
    lastGdbSignal = SIGTRAP;
    ProcessGdbException();
  cleanup:
    //    sigaction(SIGSEGV, &sigsegv_buf, 0);
    //    sigaction(SIGBUS,  &sigbus_buf,  0);
    ;
  }
#endif
}
  
