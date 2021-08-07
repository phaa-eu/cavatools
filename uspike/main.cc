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
  argc -= i;
  argv += i;
}

static arg<const char*> isa("isa=", "rv64imafdcv", "RISC-V instruction set architecture string");
static arg<const char*> vec("vec=", "vlen:128,elen:64,slen:128", "Vector unit parameters");

insnSpace_t insn;

void insnSpace_t::init(long lo, long hi)
{
  base=lo;
  limit=hi;
  int n = (hi-lo)/2;
  predecoded=new Insn_t[n];
  memset(predecoded, 0, n*sizeof(Insn_t));
}

int main(int argc, const char* argv[], const char* envp[])
{
  fprintf(stderr, "isa=%s, vec=%s\n", isa.val(), vec.val());
  parse_arguments(argc, argv);
  fprintf(stderr, "isa=%s, vec=%s\n", isa.val(), vec.val());
  processor_t* p = new processor_t(isa.val(), "mu", vec.val(), 0, 0, false, stdout);
  STATE.prv = PRV_U;
  STATE.mstatus |= (MSTATUS_FS|MSTATUS_VS);
  STATE.vsstatus |= SSTATUS_FS;
  long entry = load_elf_binary(argv[0], 1);
  insn.init(low_bound, high_bound);
  WRITE_REG(2, initialize_stack(argc, argv, envp, entry));
  long pc = entry;
  long insn_count = 0;
  while (1) {
    try {
      while (1) {
	//fprintf(stderr, "%8ld ", insn_count);
	Insn_t i = insn.at(pc);
	long oldpc = pc;
	pc = emulate[i.op_code](pc, p);
	insn_count++;
	//disasm(oldpc);
      }
    } catch(trap_user_ecall& e) {
      WRITE_REG(10, proxy_ecall(READ_REG(17),
				insn_count,
				READ_REG(10),
				READ_REG(11),
				READ_REG(12),
				READ_REG(13),
				READ_REG(14),
				READ_REG(15)));
      pc += 4;
      insn_count++;
    }
  }
  return 0;
}
