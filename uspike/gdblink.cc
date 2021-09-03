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

#include "uspike.h"
#include "cpu.h"

//	E01 - Command syntax error.
//	E02 - Error in hex data.

//#define INBUFSIZE    ((NUMREGS+1)*16 + 100)
//#define OUTBUFSIZE   ((NUMREGS+1)*16 + 100)

#define INBUFSIZE    4096
#define OUTBUFSIZE   4096

static char inBuf[INBUFSIZE];
static char outBuf[OUTBUFSIZE];

static char* inPtr = inBuf;
static char* outPtr = outBuf;

static int lastSignal =0;	// For '?' command
static jmp_buf bombed;

#define memory 0

static void
LocalExceptionHandler(int signum) {
  fprintf(stderr, "LocalExceptionHandler called\n");
  longjmp(bombed, signum);
}

static struct sigaction localaction = { &LocalExceptionHandler, };

static int tcpLink;

static long
getDebugChar() {
  char buf;
  long rc;
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
putDebugChar(long ch) {
  long rc;
  do {
    //fprlongf(stderr, "%c", ch);
    rc = write(tcpLink, &ch, 1);
    if (rc < 0) {
      perror("putDebugChar");
      abort();
    }
  } while (rc < 1);
}


void
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
  perror("socket");
  if (tmp_desc < 0)
    perror ("Can't open socket");

      /* Allow rapid reuse of this port. */
  tmp = 1;
  setsockopt (tmp_desc, SOL_SOCKET, SO_REUSEADDR, (char *) &tmp,
	      sizeof (tmp));
  perror("setsockopt");

  sockaddr.sin_family = PF_INET;
  sockaddr.sin_port = htons (port);
  sockaddr.sin_addr.s_addr = INADDR_ANY;

  if (bind (tmp_desc, (struct sockaddr *) &sockaddr, sizeof (sockaddr))
      || listen (tmp_desc, 1))
    perror ("Can't bind address");
  perror("bind");

  tmp = sizeof (sockaddr);
  tcpLink = accept (tmp_desc, (struct sockaddr *) &sockaddr, &tmp);
  if (tcpLink == -1)
    perror ("Accept failed");
  perror("accept");

  protoent = getprotobyname ("tcp");
  if (!protoent)
    perror ("getprotobyname");

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


static long
val2hexch(long val) {
  val &= 0xF;
  if (0 <= val && val <= 9)
    return val + '0';
  else
    return val - 10 + 'a';
}

static long
hexch2val(long ch) {
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
  long ch;
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
  //  fprintf(stderr, "ReceivePacket: `%s'...", inBuf);
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

  //  fprintf(stderr, "OK\n");
}

static void
SendPacket() {
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
  //  fprintf(stderr, "SendPacket: `%s'\n", outBuf);
}

static void
Reply(const char* msg) {
  while (*msg)
    *outPtr++ = *msg++;
  *outPtr = '\0';
}

static void
ReplyInHex(void* address, long bytes) {
  fprintf(stderr, "ReplyInHex(address=%p, bytes=%ld)\n", address, bytes);
  if ((long)address < (long)low_bound) {
    Reply("E09");
    return;
  }
    
  char* p = (char*)address;
  fprintf(stderr, "just before alloca\n");
  char* tmpbuf = (char*)alloca(bytes);
  if (tmpbuf == NULL)
    abort();

  if (outPtr + bytes > outBuf + OUTBUFSIZE-1)
    abort();			// Internal error: output buffer overflow.

  /* Check addresses. */
  {
    struct sigaction sigsegv_buf, sigbus_buf;
    long k;
    fprintf(stderr, "just before setjmp\n");
    if (setjmp(bombed)) {
      if (sigaction(SIGSEGV, &sigsegv_buf, 0))
	perror("sigaction(SIGSEGV)");
      if (sigaction(SIGBUS,  &sigbus_buf,  0))
	perror("sigaction(SIGBUS)");
      Reply("E09");
      return;
    }
    if (sigaction(SIGSEGV, &localaction, &sigsegv_buf))
	perror("sigaction(SIGSEGV)");
    if (sigaction(SIGBUS,  &localaction, &sigbus_buf))
	perror("sigaction(SIGBUS)");
    fprintf(stderr, "sigactions were ok\n");
    for (k=0; k<bytes; k++) {
      tmpbuf[k] = *p++;
    }
    if (sigaction(SIGSEGV, &sigsegv_buf, 0))
	perror("sigaction(SIGSEGV)");
    if (sigaction(SIGBUS,  &sigbus_buf,  0))
	perror("sigaction(SIGBUS)");
  }
  p = tmpbuf;
  while (bytes-- > 0) {
    *outPtr++ = val2hexch(*p >> 4);
    *outPtr++ = val2hexch(*p++ & 0xF);
  }
  *outPtr = '\0';
}

static void
ReplyInt(long v, long bytes) {
  long hexdigits = 2 * bytes;
  if (outPtr + hexdigits > outBuf + OUTBUFSIZE-1)
    abort();			// Internal error: output buffer overflow.
  while (hexdigits-- > 0) {
    *outPtr++ = val2hexch((v >> 4*hexdigits) & 0xF);
  }
  *outPtr = '\0';
}

static long
RcvWord(const char* match) {
  if (strncmp(inPtr, match, strlen(match)) == 0)
    return 1;
  else
    return 0;
}

static long
RcvHexDigit(long* value) {
  long digit;
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

static long
RcvHexInt(long* ptr) {
  long value;
  long digit;
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

static long
RcvHexToMemory(void* address, long bytes) {
  unsigned char* p = (unsigned char*)address;

  /* Check addresses by writing zeros. */
  {
    struct sigaction sigsegv_buf, sigbus_buf;
    long k;
    sigaction(SIGSEGV, &localaction, &sigsegv_buf);
    sigaction(SIGBUS,  &localaction, &sigbus_buf);
    if (setjmp(bombed)) {
      sigaction(SIGSEGV, &sigsegv_buf, 0);
      sigaction(SIGBUS,  &sigbus_buf,  0);
      return 0;
    }
    for (k=0; k<bytes; k++)
      p[k] = 0;
    sigaction(SIGSEGV, &sigsegv_buf, 0);
    sigaction(SIGBUS,  &sigbus_buf,  0);
  }
  p = (unsigned char*)address;
  while (bytes-- > 0) {
    long value;
    long digit;
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

void
ProcessGdbCommand(cpu_t* theCPU)
{
  for (;;) {
    ReceivePacket();		// Resets inPtr to beginning of inBuffer.
    switch (*inPtr++) {
    case 'g':			// Send all CPU register values to gdb.
      printf("GDB_COMMAND: g\n");
      //ReplyInHex((void*)&theCPU->gpr, (NUMREGS+1)*8);
      ReplyInHex(theCPU->reg_file(), 32*8);
      //ReplyInHex((void*)&theCPU->pc, 8);
      ReplyInt(theCPU->read_pc(), 8);
      break;
    case 'G':			// gdb sets all CPU register values.
      printf("GDB_COMMAND: G\n");
      //if (RcvHexToMemory((void*)&theCPU->gpr, (NUMREGS+1)*8))
      if (RcvHexToMemory(theCPU->reg_file(), 32*8))
	Reply("OK");
      else
	Reply("E02");
      break;
    case '?':			// gdb asks what was last signal.
      printf("GDB_COMMAND: ?\n");
      Reply("S");
      ReplyInt(lastSignal, 1);
      break;

    case 'P':			// Prr=VVVV - gdb sets single CPU register rr.
      {
	printf("GDB_COMMAND: P\n");
	long regno;
	//if (RcvHexInt(&regno) && *inPtr++ == '=' && 0 <= regno && regno < NUMREGS) {
	//  if (RcvHexToMemory((void*)&theCPU->gpr[regno].j, 8))
	if (RcvHexInt(&regno) && *inPtr++ == '=' && 0 <= regno && regno < 32) {
	  long value;
	  if (RcvHexToMemory(&value, 8)) {
	    theCPU->write_reg(regno, value);
	    Reply("OK");
	  }
	  else
	    Reply("E02");
	}
	else
	  Reply("E01");
      }
      break;
    case 'm':			// mAAAA,LLL - send LLL bytes at AAAA to gdb.
      {
	printf("GDB_COMMAND: m\n");
	long addr;		// Beginning address.
	long length;		// Number of bytes.
	if (RcvHexInt(&addr) && *inPtr++ == ',' && RcvHexInt(&length)) {
	  //ReplyInHex((char*) memory + addr, length);
	  ReplyInHex((char*)addr, length);
	}
	else
	  Reply("E01");
      }
      break;
    case 'M':			// Gdb writes LLL bytes at AA..AA.
      {
	printf("GDB_COMMAND: M\n");
	long addr;		// Beginning address.
	long length;		// Number of bytes.
	if (RcvHexInt(&addr) && *inPtr++ == ',' && RcvHexInt(&length) && *inPtr++ == ':') {
	  //if (RcvHexToMemory((char*) memory + addr, length))
	  if (code.valid(addr) && length <= 4) {
	    long buf = 0;
	    if (RcvHexToMemory(&buf, length)) {
	      if (code.valid(addr))
		code.set(addr, decoder(buf, addr));
	      Reply("OK");
	    }
	    else
	      Reply("E02");
	  }
	  else {
	    if (RcvHexToMemory((char*)addr, length))
	      Reply("OK");
	    else
	      Reply("E02");
	  }
	}
	else
	  Reply("E01");
      }
      break;
    case 's':			// Single step.
      //theCPU->single_step = true;
      {
	printf("GDB_COMMAND: s\n");
	long addr;
	if (*inPtr == '\0')
	  return;		// Continue at current pc.
	else if (RcvHexInt(&addr)) {
	  //theCPU->pc = addr;
	  theCPU->write_pc(addr);
	  return;		// Continue at specified pc.
	}
	else
	  Reply("E01");
	//theCPU->single_step = state_t::STEP_STEPPING;
      }
      break;

    case 'c':			// cAA..AA - continue at address AA..AA
      printf("GDB_COMMAND: c\n");
      //theCPU->single_step = false;
      //theCPU->single_step = state_t::STEP_NONE;
      {
	long addr;
	if (*inPtr == '\0')
	  return;		// Continue at current pc.
	else if (RcvHexInt(&addr)) {
	  //theCPU->pc = addr;
	  theCPU->write_pc(addr);
	  return;		// Continue at specified pc.
	}
	else
	  Reply("E01");
      }
      break;

    } /* switch */
    SendPacket();		// Resets outPtr to beginning of outBuffer.
  } /* for (;;) */
}


void
HandleException(int signum) {
  fprintf(stderr, "HandleException(%d)\n", signum);
  //fprintf(stderr, "Current pc = %lx\n", theCPU->pc);
  cpu_t* theCPU = cpu_t::find(gettid());
  fprintf(stderr, "Current pc = %lx\n", theCPU->read_pc());
  lastSignal = signum;

  Reply("S");
  ReplyInt(signum, 1);	// signal number
  SendPacket();			// Resets outPtr.
  return;

  Reply("T");
  ReplyInt(signum, 1);	// signal number
  Reply("20:");		// PC is register 32 (see riscv-tdep.c)
  ReplyInt(theCPU->read_pc(), 8);
  Reply(";");
  SendPacket();			// Resets outPtr.
  //  ProcessGdbCommand();
}


