/*
  Copyright (c) 2021 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

class options_t {
  static options_t* list;
  static const char* title;
  options_t* next;
  const char* name;
  const char* explain;
  bool no_equal;
 public:
  options_t(const char* n, const char* e, bool nv) { next=list; list=this; name=n; explain=e; no_equal=nv; }
  void help();
  bool matches(const char* opt);
  virtual void setval(const char* v) =0;
  virtual void printval() =0;
  friend void parse_options(int &argc, const char**& argv, const char* t);
  static void help_exit();
};

template <class T =const char*>
class option : public options_t {
  T value;			// --option=value
  T none;			// --option with no value
public:
  option(const char* n, T ini,        const char* e) : options_t(n, e, false) { value=ini;           }
  option(const char* n, T ini, T def, const char* e) : options_t(n, e, true ) { value=ini; none=def; }
  T val() { return value; }
  void setval(const char* v);
  void printval();
};

void parse_options(int &argc, const char**& argv, const char* t);
inline void help_exit() { options_t::help_exit(); }
