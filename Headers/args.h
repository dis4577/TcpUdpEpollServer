#ifndef ARGS_H
#define ARGS_H

#include <map>
#include <unistd.h>

void getArgs(int argc, char* argv[], std::map<char, std::string>* runArgs);

#endif // ARGS_H
