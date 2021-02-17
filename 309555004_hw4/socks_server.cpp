#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/asio/ip/address.hpp>
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <sys/wait.h>

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#include <fcntl.h>
#include <string.h>
#include <string>

#include <vector>

#define CONNECT 1
#define BIND 2
#define ACCEPT 5
#define REJECT 6
#define BROW_TO_DIST 11
#define DIST_TO_BROW 12
#define TO_BOTH 13

bool output_transfer_data = false;
bool output_firewall_data = false;
bool test_mode_so_output = false;

using boost::asio::ip::tcp;
using namespace std;

boost::asio::io_service global_io_service;

class PERMIT{
    public:
        int OPERATION;
        string IP[4];
};

class FIREWALL{
    public:
        PERMIT permit[100];
        int count;
};

FIREWALL firewall;

void set_firewall();
int check_firewall(string &ip_addr, string require_operation);


class socks_session  :  public std::enable_shared_from_this<socks_session>{
    private:
        tcp::socket socket_;
        tcp::socket dst_socket_;

        enum { max_length = 4096 };
        unsigned char request[max_length];
        unsigned char reply[8] = {0x00};

        //for transmit data
        char buf_Dist[max_length];
        char buf_Brow[max_length];

        //for connect connection with dst_server during connect operation
        tcp::resolver dst_resolver_;

        //for inbound connection during bind operation
        unsigned short bind_port_;
        tcp::acceptor bind_acceptor_;

        string SRCIP = "";
        string SRCPORT = "";
        string DSTIP = "";
        string DSTPORT = "";
        string COMMAND = "";
        string RESULT = "";
        bool is_domain_name = false;

    public:
        socks_session(tcp::socket socket) : socket_(std::move(socket)), dst_socket_(global_io_service),
                                            dst_resolver_(global_io_service), bind_acceptor_(global_io_service) { }
        void start() {
            do_read_request();
        }

    private:
        void do_read_request() {
            auto self(shared_from_this());
            if (test_mode_so_output == true) cerr << "do read request" << endl;
            memset(request, '\0', max_length);
            socket_.async_read_some(boost::asio::buffer(request, max_length),
                                    [this, self](boost::system::error_code ec, std::size_t length) {
                                        if (!ec) {
                                            if (request[0] == 0x04){
                                                if (test_mode_so_output == true){
                                                    cerr << "do handle request from browser, request=";
                                                    for (int i=0; i<8; i++) cerr << to_string(request[i]) << " ";
                                                    cerr << endl << endl;
                                                }

                                                set_firewall();
                                                do_handle_request(request);
                                            }
                                        }
                                        else {
                                            if (test_mode_so_output == true) cerr << "Error (do_read_request): " << ec.message() << "\n";
                                        }
                                        // do_read_request();

                                    } );
        }

        void do_handle_request(unsigned char request[]){
            auto self(shared_from_this());
            if (test_mode_so_output == true) cerr << "do handle request" << endl;
            unsigned char VN = request[0];
            if(VN!=0x04 && (test_mode_so_output == true)) cerr << "[VN] wrong value: " << VN << endl;

            unsigned char CD = request[1];
            int require_operation = 0;
            if(CD == 0x01) require_operation = CONNECT;
            else if(CD == 0x02) require_operation = BIND;
            else if (test_mode_so_output == true) cerr << "[CD] wrong value: " << CD << endl;

            SRCIP = socket_.remote_endpoint().address().to_string();
            SRCPORT = to_string(socket_.remote_endpoint().port());

            COMMAND = (require_operation == CONNECT) ? "CONNECT" : ((require_operation == BIND) ? "BIND" : "NEITHER") ;
            // RESULT = (check_firewall(SRCIP, require_operation) == ACCEPT)&&(VN==0x04) ? "Accept" : "Reject";

            DSTPORT = to_string((int)(request[2]<<8) + (int)(request[3]));
            DSTIP = "";


            if (request[4] == 0x00 && request[5] == 0x00 && request[6] == 0x00 && request[7] != 0x00) {
                is_domain_name = true;
                for (int i=8; i<max_length; i++){
                    if (request[i]) DSTIP.push_back(request[i]);
                }
            }
            else {
                DSTIP = to_string(request[4]) + "." + to_string(request[5]) + "." + to_string(request[6]) + "." + to_string(request[7]);
            }

            tcp::resolver::query q(DSTIP, DSTPORT);
            dst_resolver_.async_resolve(q,
                                        [this, self](boost::system::error_code ec, tcp::resolver::iterator itr) {
                                            if (!ec)  do_resolve_handler(itr);
                                            else if (test_mode_so_output == true) cerr << "Error (async_resolve): " << ec.message() << "\n";

                                        });

        }


        void do_resolve_handler(tcp::resolver::iterator itr){
            auto self(shared_from_this());

            if (test_mode_so_output == true) cerr << "do resolve handler" << endl;

            while(is_domain_name == true && itr!=tcp::resolver::iterator() ){
                boost::asio::ip::address addr = itr->endpoint().address();
                if(addr.is_v4()){
                    DSTIP = addr.to_string();
                    break;
                }
                itr++;
            }
            RESULT = (check_firewall(DSTIP, COMMAND) == ACCEPT) ? "Accept" : "Reject";

            cout << endl;
  	        if (test_mode_so_output == true) cout << "+------------------------------------------------------+" << endl;
            cout << "<S_IP>: "    << SRCIP << endl;
            cout << "<S_PORT>: "  << SRCPORT << endl;
            cout << "<D_IP>: "    << DSTIP << endl;
            cout << "<D_PORT>: "  << DSTPORT << endl;
            cout << "<Command>: " << COMMAND << endl;
            cout << "<Reply>: "   << RESULT << endl;
  	        if (test_mode_so_output == true) cout << "+------------------------------------------------------+" << endl;
            cout << endl;

            // cerr << "addr= \""<< itr->endpoint().address() << "\"" << endl;

            if(RESULT == "Accept"){

                if (COMMAND == "CONNECT")  {
                    tcp::endpoint dst_endpoint = *itr;
                    dst_socket_.async_connect(dst_endpoint,
                                  [this, self](boost::system::error_code ec){
                                        if (!ec) do_reply(1);
                                        else if (test_mode_so_output == true) cerr << "Error (async_connect): " << ec.message() << "\n";

                                    });
                }
                else if(COMMAND == "BIND"){
                    boost::system::error_code ec;
                    unsigned short free_port(0);

                    tcp::endpoint loc_endpoint(tcp::v4(), free_port);

                    bind_acceptor_.open( loc_endpoint.protocol(), ec);
                    if (ec && (test_mode_so_output == true))  cerr << "Error (open): " << ec.message() << "\n";
                    bind_acceptor_.set_option(tcp::acceptor::reuse_address(true));
                    bind_acceptor_.bind(loc_endpoint, ec);
                    if (ec && (test_mode_so_output == true))  {
                        cerr << "Error (bind): " << ec.message() << "\n";
                        cerr << "bind addr: " << loc_endpoint.address().to_string() << ", port=" << loc_endpoint.port() << endl;
                    }
                    bind_acceptor_.listen(boost::asio::socket_base::max_connections, ec);
                    if (ec && (test_mode_so_output == true))  cerr << "Error (listen): " << ec.message() << "\n";

                    bind_port_ = bind_acceptor_.local_endpoint().port();

                    do_reply(1); //  first time

                }
            }
            else if(RESULT == "Reject") {
                do_reply(1);
                do_read_request();
            }
        }

        void do_accept_bind(){
            auto self(shared_from_this());
            bind_acceptor_.async_accept(dst_socket_,
                                        [this, self](boost::system::error_code ec){
                                            if (!ec) {
                                                do_reply(2);
                                            }
                                            else if (test_mode_so_output == true) cerr << "Error (async_accept): " << ec.message() << "\n";
                                        });
        }

        void do_reply(int times){
            auto self(shared_from_this());

            if(COMMAND == "CONNECT"){
                for(int i=0; i<8; i++) reply[i] = 0x00;
            }
            else if(COMMAND == "BIND"){
                reply[0] = 0x00;
                reply[2] = (unsigned char)(bind_port_/256);
                reply[3] = (unsigned char)(bind_port_%256);
                for(int i=4; i<8; i++) reply[i] = 0x00;
            }

            if(RESULT == "Accept") reply[1] = 0x5A;
            else if(RESULT == "Reject") reply[1] = 0x5B;

            if (test_mode_so_output == true) {
                cerr << "do_reply, reply= ";
                for (int i=0; i<8; i++) cerr << to_string(reply[i]) << " ";
                cerr << endl << endl;
            }

            async_write(socket_, boost::asio::buffer(reply, 8),
                                [this, self, times](boost::system::error_code ec, std::size_t) {
                                    if(RESULT == "Accept" && COMMAND == "CONNECT") do_read(TO_BOTH);
                                    if(RESULT == "Accept" && COMMAND == "BIND" && times == 1) do_accept_bind();
                                    if(RESULT == "Accept" && COMMAND == "BIND" && times == 2) do_read(TO_BOTH);
                                    else if(ec && (test_mode_so_output == true))  cerr << "Error (async_write): " << ec.message() << "\n";
                                });
        }

        void do_read(int direction){
            auto self(shared_from_this());

            if(direction == BROW_TO_DIST || direction == TO_BOTH){
                if (output_transfer_data == true) cerr << "do read (BROW): ";
                // bzero(buf_Dist, sizeof(buf_Dist));
                memset(buf_Dist, '\0', max_length);
                socket_.async_read_some(boost::asio::buffer(buf_Dist, max_length),
                                    [this, self](boost::system::error_code ec, std::size_t length) {
                                        if (!ec){
                                            if (output_transfer_data == true) cerr  << "\"" << string(buf_Dist) << "\""<< endl << endl;
                                            do_write(BROW_TO_DIST, length);
                                        }
                                        else{

                                            if (COMMAND == "CONNECT") {
                                                if (ec != boost::asio::error::eof && (test_mode_so_output == true))
                                                    cerr << "[Error] " << ec.message() << "\n";
                                                socket_.close();
                                                dst_socket_.close();
                                            }
                                            if (COMMAND == "BIND"){
                                                if (ec == boost::asio::error::eof){
                                                    async_write(dst_socket_, boost::asio::buffer(buf_Dist, length),
                                                                [this, self](boost::system::error_code ec, std::size_t ) {
                                                                    if (ec && (test_mode_so_output == true)){
                                                                        cerr << "send eof error!" << ec.message() << endl;
                                                                    }
                                                                });
                                                    dst_socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_send, ec);
                                                }
                                                else if (test_mode_so_output == true) cerr << "[Error] " << ec.message() << "\n";
                                            }

                                        }

                                    } );
            }
            if(direction == DIST_TO_BROW || direction == TO_BOTH){
                if (output_transfer_data == true) cerr << "do read (DIST): ";
                // bzero(buf_Brow, sizeof(buf_Brow));
                memset(buf_Brow, '\0', max_length);
                dst_socket_.async_read_some(boost::asio::buffer(buf_Brow, max_length),
                                    [this, self](boost::system::error_code ec, std::size_t length) {
                                        if (!ec) {
                                            if (output_transfer_data == true) cerr  << "\"" << string(buf_Brow) << "\""<< endl << endl;
                                            do_write(DIST_TO_BROW, length);
                                        }
                                        else {

                                            if (COMMAND == "CONNECT") {
                                                if (ec != boost::asio::error::eof && (test_mode_so_output == true))
                                                    cerr << "[Error] " << ec.message() << "\n";
                                                socket_.close();
                                                dst_socket_.close();
                                            }
                                            if (COMMAND == "BIND"){
                                                if (ec == boost::asio::error::eof){
                                                    async_write(socket_, boost::asio::buffer(buf_Brow, length),
                                                                [this, self](boost::system::error_code ec, std::size_t ) {
                                                                    if (ec && (test_mode_so_output == true)){
                                                                        cerr << "send eof error!" << ec.message() << endl;
                                                                    }
                                                                });
                                                    socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_send, ec);
                                                }
                                                else if (test_mode_so_output == true) cerr << "[Error] " << ec.message() << "\n";
                                            }

                                        }

                                    } );
            }

        }

        void do_write(int direction, int len){
            auto self(shared_from_this());

            if(direction == BROW_TO_DIST){
                string str = buf_Dist;
                if (output_transfer_data == true){
                    cerr << "do write (DIST): \"" << str << "\"" << endl << endl;
                }
                async_write(dst_socket_, boost::asio::buffer(buf_Dist, len),
                                    [this, self, str, len](boost::system::error_code ec, std::size_t ) {
                                        if (!ec) {
                                            do_read(BROW_TO_DIST);
                                        }
                                        else {
                                            if (test_mode_so_output == true)
                                                cerr << "Error (async_write): " << ec.message() << "\n";

                                            socket_.close();
                                            dst_socket_.close();
                                        }
                                    } );
            }
            if(direction == DIST_TO_BROW){
                string str = buf_Brow;
                if (output_transfer_data == true){
                    cerr << "do write (BROW): \"" << str << "\"" << endl << endl;
                }
                async_write(socket_, boost::asio::buffer(buf_Brow, len),
                                    [this, self, str, len](boost::system::error_code ec, std::size_t ) {
                                        if (!ec) {
                                            do_read(DIST_TO_BROW);
                                        }
                                        else {
                                            if (test_mode_so_output == true)
                                                cerr << "Error (async_write): " << ec.message() << "\n";

                                            socket_.close();
                                            dst_socket_.close();
                                        }


                                    } );
            }
        }

};

class socks_server {
    private:
        tcp::socket socket_;
        tcp::acceptor acceptor_;

        int sockfd, childpid, status;

    public:
        socks_server(short port) : acceptor_(global_io_service, tcp::endpoint(tcp::v4(), port)), socket_(global_io_service) {
            // sockfd = socket_.native_handle();
            do_accept();

        }

    private:
        void do_accept() {
            //prevent zombie occuring
            signal(SIGCHLD, SIG_IGN);

            acceptor_.async_accept(socket_, [this](boost::system::error_code ec) {
                if (!ec) {
                    waitpid(-1, &status, WNOHANG);
                    global_io_service.notify_fork(boost::asio::io_service::fork_prepare);

                    childpid = fork();

                    if(childpid == 0){
                        global_io_service.notify_fork(boost::asio::io_service::fork_child);
                        std::make_shared<socks_session>(std::move(socket_))->start();
                        acceptor_.close();
                    }
                    else if (childpid>0) {
                        global_io_service.notify_fork(boost::asio::io_service::fork_parent);
                        socket_.close();
                        waitpid(-1, &status, WNOHANG) ;
                    }
                    else { //childpid < 0, err
                        while((waitpid(-1, &status, WNOHANG)) > 0){
                        }

                    }
                }
                do_accept();
            } );
        }
};

int main(int argc, char* argv[]){
    try {
        if (argc != 2) {
            cerr << "Usage: async_tcp_echo_server <port>\n";
            return 1;
        }

        socks_server socks_s(std::atoi(argv[1]));
        global_io_service.run();
    }

    catch (std::exception& e) {
        cerr << "Exception: " << e.what() << "\n";
    }

    return 0;
}

void set_firewall(){
    if (test_mode_so_output == true) cerr << "set firewall: ";
    ifstream file("socks.conf");
    string line;
    int cnt=0;
    while(getline(file, line)){
        vector<string> vec;
        stringstream input(line);
        string tmp;
        while(input>>tmp){
            vec.push_back(tmp);
        }
        if(vec.size() == 3){

            if (vec[1] == "c") firewall.permit[cnt].OPERATION = CONNECT;
            else if(vec[1] == "b") firewall.permit[cnt].OPERATION = BIND;
            else firewall.permit[cnt].OPERATION = -1;

            string IP = vec[2];
            if (output_firewall_data == true) cerr << "IP= " << IP << endl;

            firewall.permit[cnt].IP[0] = strtok(const_cast<char *>(IP.c_str()), ".");
            firewall.permit[cnt].IP[1] = strtok(NULL, ".");
            firewall.permit[cnt].IP[2] = strtok(NULL, ".");
            firewall.permit[cnt].IP[3] = strtok(NULL, ".");

            if (output_firewall_data == true){
                cerr << "OP= " << firewall.permit[cnt].OPERATION << ", IP=";
                for (int mn=0; mn<4; mn++)
                    cerr << firewall.permit[cnt].IP[mn] << " ";
                cerr << endl;
            }

            cnt++;

        }
        vec.clear();
    }
    firewall.count = cnt;
}

int check_firewall(string &ip_addr, string require_operation){
    if(ip_addr=="" && (test_mode_so_output == true)) {cerr << "ip_addr is null" << endl;  return -1; }

    string ip[4];
    ip[0] = strtok(const_cast<char *>(ip_addr.c_str()), ".");
    ip[1] = strtok(NULL, ".");
    ip[2] = strtok(NULL, ".");
    ip[3] = strtok(NULL, ".");
    ip_addr = ip[0]+"."+ip[1]+"."+ip[2]+"."+ip[3];

    if (output_firewall_data == true) {
        cerr << "check firewall: reqOP= " << require_operation << ", IP= ";
        for (int mn=0; mn<4; mn++) cerr << ip[mn] << " ";
        cerr << endl;
    }

    int op_value = (require_operation == "CONNECT") ? CONNECT : ((require_operation == "BIND") ? BIND : -1) ;

    for(int i=0; i<firewall.count; i++){
        if(firewall.permit[i].OPERATION != op_value) continue;

        for(int j=0; j<4; j++){
            if(firewall.permit[i].IP[j] == "*"){
                if (j==3) {
                    return ACCEPT;

                }
                else continue;
            }
            else if(ip[j] == firewall.permit[i].IP[j]){
                if (j==3) {
                    return ACCEPT;
                }
                else continue;
            }
            else break;
        }
    }
    return REJECT;
}



