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

arg_base_t* arg_base_t::arg_list =0;
const char* trap_t::name() { return 0; }

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
}

static arg<const char*> isa("isa=", "rv64imafdcv", "RISC-V instruction set architecture string");
static arg<const char*> vec("vec=", "vlen:128,elen:64,slen:128", "Vector unit parameters");

insnSpace_t insn;

void insnSpace_t::predecode(long lo, long hi)
{
  base=lo;
  limit=hi;
  predecoded=new Insn_t[(hi-lo)/2];
  for (long pc=lo; pc<hi; ) {
    predecoded[(pc-lo)/2] = decoder(pc);
    pc += 2*(predecoded[(pc-lo)/2].op_4B+1);
  }
}

int main(int argc, const char* argv[], const char* envp[])
{
  parse_arguments(argc, argv);
  processor_t* p = new processor_t(isa.val(), "mu", vec.val(), 0, 0, false, stdout);
  long pc;
  long low_bound, high_bound;
  load_elf_binary(argv[0], pc, low_bound, high_bound);
  insn.predecode(low_bound, high_bound);
  WRITE_REG(2, initialize_stack(argc, argv, envp, pc));
  while (1) {
    Insn_t i = decoder(pc);
    pc = emulate[i.op_code](pc, p);
  }
  return 0;
}
