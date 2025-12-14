#include <Headers/server.h>
#include <Headers/args.h>

int main(int argc, char* argv[])
{
    __socket_type sockType = SOCK_STREAM;
    int port = 8080;

    // e.g. ./server -t tcp -p 8080
    std::map<char, std::string> runArgs;
    getArgs(argc, argv, &runArgs);
    if (runArgs.size() > 0)
    {
        try
        {
            if(runArgs.at('t') == "tcp"){
                sockType = SOCK_STREAM;
            }
            else if(runArgs.at('t') == "udp"){
                sockType = SOCK_DGRAM;
            }

            port = std::stoi(runArgs.at('p'));
        }
        catch (std::exception& e){
            std::cout << "Invalid arguments. Run by default" << std::endl;
        }
    }

    int result = 0;
    Server serv;
    result = serv.init(sockType, port);
    if(result != 0){
        return result;
    }

    result = serv.run();

    return result;
}
