#include <Headers/args.h>

void getArgs(int argc, char* argv[], std::map<char, std::string>* runArgs)
{
    for (int opt = -1; (opt = getopt(argc, argv, "t:p:")) != -1;)
    {
        if(opt == 't')
        {
            if(optarg){
                runArgs->insert({opt, optarg});
            }
        }
        else if(opt == 'p')
        {
            if(optarg){
                runArgs->insert({opt, optarg});
            }
        }
    }
}
