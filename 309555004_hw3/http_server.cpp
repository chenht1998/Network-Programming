#include <cstdlib>
#include <iostream>
#include <memory>
#include <utility>
#include <boost/asio.hpp>

#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <sys/wait.h> //wait
#include <string>
#include <vector>


using boost::asio::ip::tcp;
using std::string;
using std::vector;
using std::cin;
using std::cout;
using std::cerr;
using std::endl;
using std::flush;
using std::stringstream;

char ** test_argv;

class REQUEST{
    public:
        string METHOD;

        string URI;

        string QUERY_STRING;

        string SERVER_PROTOCOL;

        string HTTP_HOST;
        string SERVER_ADDR;
        string SERVER_PORT;

        string REMOTE_ADDR;
        string REMOTE_PORT;
        string cgifile;

};

vector<string> splitRequest(char* request){
    vector<string> vec;
    string line = request;
    string tmp;
    stringstream input(line);
    while(input>>tmp){
        vec.push_back(tmp);
        // cout << tmp << " ";
    }
    return vec;
}

string splitByDelim(string &remain_str, char *delim){
    string tmp = remain_str;
    char *query = new char[tmp.length()+1];
    strcpy(query, tmp.c_str());

    string splitOut = strsep(&query, delim);
    if (query==NULL) remain_str="\0";
    else remain_str = query;

    return splitOut;
}

REQUEST parseEnvVar(vector<string> ctxt){
    REQUEST req;
    vector<string>::iterator itr = ctxt.begin();

    // REQUEST_METHOD
    req.METHOD = *itr;  // GET or POST or else
    itr++;

    // REQUEST_URI ? QUERY_STRING
    string URI_n_QUERY = *itr;

    req.URI = URI_n_QUERY;

    if(strstr(URI_n_QUERY.c_str(), "?")!=NULL){
        req.cgifile = strtok(const_cast<char *>(URI_n_QUERY.c_str()), "?"); //const_cast可以跨過 const 訪問原始變數
        req.QUERY_STRING = strtok(NULL, " \n\r");
        req.URI = req.cgifile+"?"+req.QUERY_STRING;
    }
    else req.cgifile = req.URI;

    itr++;

    // SERVER_PROTOCOL
    req.SERVER_PROTOCOL = *itr;
    itr++;

    // HTTP_HOST
    string HTTP_HOST_str;
    for (; itr!=ctxt.end(); ){
        if(*itr=="Host:"){
            itr++;
            HTTP_HOST_str = *itr;
            itr++;
            break;
        }
        else itr++;
    }

    // req.SERVER_ADDR = strtok(const_cast<char *>(HTTP_HOST_str.c_str()), ":"); //const_cast可以跨過 const 訪問原始變數
    // req.SERVER_PORT = strtok(NULL, " \n\r");
    // req.HTTP_HOST = req.SERVER_ADDR + ":" + req.SERVER_PORT;
    req.HTTP_HOST = HTTP_HOST_str;

    return req;
}

REQUEST parse(char* request){
    vector<string> ctxt =  splitRequest(request);
    REQUEST req = parseEnvVar(ctxt);
    return req;
}

void check(int val, string errmsg){
    if(val<0) cout << errmsg;
}

void setSocketEnv(REQUEST req){
    check(setenv("REQUEST_METHOD", req.METHOD.c_str(), 1), "REQUEST_METHOD err\n");
    check(setenv("REQUEST_URI", req.URI.c_str(), 1), "REQUEST_URI err\n");
    check(setenv("QUERY_STRING", req.QUERY_STRING.c_str(), 1), "QUERY_STRING err\n");
    check(setenv("SERVER_PROTOCOL", req.SERVER_PROTOCOL.c_str(), 1), "SERVER_PROTOCOL err\n");
    check(setenv("HTTP_HOST", req.HTTP_HOST.c_str(), 1), "HTTP_HOST err\n");
    check(setenv("SERVER_ADDR", req.SERVER_ADDR.c_str(), 1), "SERVER_ADDR err\n");
    check(setenv("SERVER_PORT", req.SERVER_PORT.c_str(), 1), "SERVER_PORT err\n");
    check(setenv("REMOTE_ADDR", req.REMOTE_ADDR.c_str(), 1), "REMOTE_ADDR err\n");
    check(setenv("REMOTE_PORT", req.REMOTE_PORT.c_str(), 1), "REMOTE_PORT err\n");
}

void printReqCtxt(REQUEST req){
    cerr << "REQUEST_METHOD=" << req.METHOD << endl;
    cerr << "REQUEST_URI=" << req.URI << endl;
    cerr << "cgifile=" << req.cgifile << endl;
    cerr << "QUERY_STRING=" << req.QUERY_STRING << endl;
    cerr << "SERVER_PROTOCOL=" << req.SERVER_PROTOCOL << endl;
    cerr << "HTTP_HOST=" << req.HTTP_HOST << endl;
    cerr << "SERVER_ADDR=" << req.SERVER_ADDR << endl;
    cerr << "SERVER_PORT=" << req.SERVER_PORT << endl;
    cerr << "REMOTE_ADDR=" << req.REMOTE_ADDR << endl;
    cerr << "REMOTE_PORT=" << req.REMOTE_PORT << endl;
}


class session  :  public std::enable_shared_from_this<session>{
    public:
        session(tcp::socket socket) : socket_(std::move(socket)) { }
        void start() {
            sockfd = socket_.native_handle();
            do_read();
        }

    private:
        void do_read() {
            auto self(shared_from_this());
            socket_.async_read_some(boost::asio::buffer(data_, max_length),
                [this, self](boost::system::error_code ec, std::size_t length) {
                    if (!ec) {
                        cerr << "data_: \n" << data_  << "data_ end\n\n";

                        childpid = fork();
                        if (childpid == 0){ //child
                            REQUEST reqCtxt = parse(data_);
                            reqCtxt.SERVER_ADDR = socket_.local_endpoint().address().to_string();
                            reqCtxt.SERVER_PORT = std::to_string(socket_.local_endpoint().port());
                            reqCtxt.REMOTE_ADDR = socket_.remote_endpoint().address().to_string();
                            reqCtxt.REMOTE_PORT = std::to_string(socket_.remote_endpoint().port());
                            setSocketEnv(reqCtxt);
                            printReqCtxt(reqCtxt);

                            dup2(sockfd, STDIN_FILENO);
                            dup2(sockfd, STDOUT_FILENO);

                            cout << reqCtxt.SERVER_PROTOCOL+" 200 OK\r\n" << flush;

                            execProc(reqCtxt.cgifile);
                        }
                        else if (childpid>0){ //parent
                            close(sockfd);
                            waitpid(childpid,&sock_status,0);
                        }
                        else{ //error
                            cerr << "fork error" << endl;
                        }

                        // do_write(length);
                    }
                }
            );
        }

        int execProc(string url){

            if (strstr(url.c_str(), ".cgi")==NULL) return -1;

            else{
                url = "."+url;
                // cerr << url.c_str() << endl;
                if(execv(url.c_str(), test_argv) <0){
                    cerr << "Execv error!\n";
                    return -1;
                }

            }
            return 0;
        }

        void do_write(std::size_t length) {
            auto self(shared_from_this());
            boost::asio::async_write(socket_, boost::asio::buffer(data_, length),
                [this, self](boost::system::error_code ec, std::size_t /*length*/) {
                    if (!ec) {

                    }
                }
            );
        }

        tcp::socket socket_;
        enum { max_length = 1024 };
        char data_[max_length];
        int sockfd, childpid, sock_status;

};

class server {
    public:
        server(boost::asio::io_context& io_context, short port)
          : acceptor_(io_context, tcp::endpoint(tcp::v4(), port)) {
            do_accept();
        }

    private:
        void do_accept() {
            acceptor_.async_accept(
                [this](boost::system::error_code ec, tcp::socket socket) {
                    if (!ec) {

                        std::make_shared<session>(std::move(socket))->start();
                    }
                    do_accept();
                }
            );
        }

        tcp::acceptor acceptor_;
};


int main(int argc, char* argv[]){
    test_argv = argv;
    try {
        if (argc != 2) {
            cerr << "Usage: async_tcp_echo_server <port>\n";
            return 1;
        }
        boost::asio::io_context io_context;

        server s(io_context, std::atoi(argv[1])); //server.do_accept() , not go async_accept in 1st loop

        io_context.run(); //do (1st)<server>async_accept, (1st)<session> start, (2nd)<server>do_accept
                          //   , (1st)<session> async_read_some
    }

    catch (std::exception& e) {
        cerr << "Exception: " << e.what() << "\n";
    }

    return 0;
}