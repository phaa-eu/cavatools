/*
  Copyright (c) 2021 Peter Hsu.  All Rights Reserved.  See LICENCE file for details.
*/
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "options.h"

template <> void option<>     ::setval(const char* v) { if (!v) value=none; else value=v;       }
template <> void option<>     ::printval() { fprintf(stderr, "%s", value); }

template <> void option<int>  ::setval(const char* v) { if (!v) value=none; else value=atoi(v); }
template <> void option<int>  ::printval() { fprintf(stderr, "%d", value); }

template <> void option<long> ::setval(const char* v) { if (!v) value=none; else value=atoi(v); }
template <> void option<long> ::printval() { fprintf(stderr, "%ld", value); }

options_t* options_t::list;
const char* options_t::title;

void parse_options(int &argc, const char**& argv, const char* t)
{
  options_t::title = t;
  int i = 1;
  while (i < argc && strncmp(argv[i], "--", 2)==0) {
    if (strcmp(argv[i], "--help") == 0)
      options_t::help_exit();
    for (options_t* p=options_t::list; p; p=p->next)
      if (p->matches(argv[i]+2))
	goto next_option;
    fprintf(stderr, "Unknown option %s\n", argv[i]);
    exit(0);
  next_option: ;
    i += 1;
  }
  argc -= i;
  argv += i;
}

bool options_t::matches(const char* opt)
{
  int len = strlen(name);
  if (strncmp(opt, name, len) != 0)
    return false;
  if (strlen(opt) <= len) {
    if (no_equal == false)
      options_t::help_exit();
    setval(0);
  }
  else if (opt[len+1] == '=')
    setval(opt+len+2);
  else
    options_t::help_exit();
  return true;
}

void options_t::help()
{
  if (next) next->help();
  fprintf(stderr, "  --%-10s %s [", name, explain);
  printval();
  fprintf(stderr, "]\n");
}

void options_t::help_exit()
{
  fprintf(stderr, "%s\n", title);
  //  if (options_t::list)
  //    options_t::list->help();
  if (list)
    list->help();
  exit(0);
}
    

