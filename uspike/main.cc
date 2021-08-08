#include <iostream>
#include <string.h>
using namespace std;

#include "uspike.h"

struct arg_base_t {
  static arg_base_t* arg_list;
  arg_base_t* next;
  virtual bool given(const char* v);
  virtual void help();
};

template <class T>
class arg : public arg_base_t {
  const char* name;
  const char* explain;
  T value;
public:
  arg(const char* n, T initial, const char* e) { name=n; value=initial; explain=e; }
  bool given(const char* v);
  T val() { return value; }
  void help() { cerr << "  " << explain << " [" << value << "]" << endl; }
};

bool arg_base_t::given(const char* v) { abort(); }
void arg_base_t::help() { abort(); }
template <> bool arg<const char*>::given(const char* v) { return strncmp(v, name, strlen(name)) ? false : (value=v+strlen(name),       true); }
template <> bool arg<int>        ::given(const char* v) { return strncmp(v, name, strlen(name)) ? false : (value=atoi(v+strlen(name)), true); }
template <> bool arg<long>       ::given(const char* v) { return strncmp(v, name, strlen(name)) ? false : (value=atoi(v+strlen(name)), true); }

arg_base_t* arg_base_t::arg_list =0;

void parse_arguments(int &argc, const char**& argv)
{
  int i = 1;
  while (i < argc && argv[i][0] == '-') {
    if (argv[i][1] != '-' || strcmp(argv[i], "--help") == 0) {
      for (arg_base_t* p=arg_base_t::arg_list; p; p=p->next)
	p->help();
      exit(0);
    }
    for (arg_base_t* p=arg_base_t::arg_list; p; p=p->next)
      if (p->given(argv[i]+2))
	goto next_option;
    cerr << "Unknown option " << argv[i] << endl;
    exit(0);
  next_option: ;
  }
  argc -= i;
  argv += i;
}

insnSpace_t code;

void insnSpace_t::init(long lo, long hi)
{
  base=lo;
  limit=hi;
  int n = (hi-lo)/2;
  predecoded=new Insn_t[n];
  memset(predecoded, 0, n*sizeof(Insn_t));
}

static arg<const char*> isa("isa=", "rv64imafdcv", "RISC-V instruction set architecture string");
static arg<const char*> vec("vec=", "vlen:128,elen:64,slen:128", "Vector unit parameters");
static arg<int>         mhz("mhz=", 1000, "Pretend MHz");
static arg<long>     report("report=", 100000000L, "Reporting frequency");

int main(int argc, const char* argv[], const char* envp[])
{
  parse_arguments(argc, argv);
  fprintf(stderr, "mhz=%d, isa=%s, vec=%s\n", mhz.val(), isa.val(), vec.val());
  start_time(mhz.val());
  long entry = load_elf_binary(argv[0], 1);
  long sp = initialize_stack(argc, argv, envp, entry);
  init_cpu(entry, sp, isa.val(), vec.val());
  enum stop_reason reason;
  long insn_count = 0;
  do {
    reason = run_insns(report.val(), insn_count);
    fprintf(stderr, "\r%ld instructions", insn_count);
  } while (reason == stop_normal);
  if (reason == stop_breakpoint)
    fprintf(stderr, "stop_breakpoint\n");
  return 0;
}

#include "constants.h"
