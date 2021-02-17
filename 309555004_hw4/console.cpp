#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <cstdlib>
#include <iostream>
#include <fstream>

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#include <fcntl.h>
#include <string.h>
#include <string>

using boost::asio::ip::tcp;
using namespace std;

#define IS_SOCKS_SERVER 10
#define ACCEPT 0x5A
#define REJECT 0x5B

class SERVER {
    public:
        string host;
        string port;
        string file;

        fstream openfile;
        int hostID;
};

class SOCKS_SERVER{
    public:
        bool is_exist;
        string host;
        string port;
};

SERVER npHost[5];
SOCKS_SERVER SocksServ;
boost::asio::io_service global_io_service;


int    hostParse(string query_str);
bool   check_read_done(string oriStr);
string getSubStrVal(string ori);
string consoleCGI(int nHost);
string output_command(string host, string content);
string output_shell(string host, string content);
string replaceSubStr(string oriStr);

class ConsoleSession  : public enable_shared_from_this<ConsoleSession>{
    private:
        tcp::resolver resolver_;
        tcp::socket socket_;

        int hostIdx_;
        enum { max_length = 4096 };
        char data_[max_length] = {0};
        bool is_exit = false;
        bool to_socks_server = false;

        unsigned char socks_request[max_length];
        unsigned char socks_reply[8];

    public:
        ConsoleSession(int hostIdx) : hostIdx_(hostIdx), resolver_(global_io_service), socket_(global_io_service) {

        }
        void start() {
            do_resolve();
        }

    private:
        void do_resolve() {
            cerr << "do_resolve " << hostIdx_ << endl;

            auto self(shared_from_this());


            string host="", port="";

            if (SocksServ.is_exist == true) {
                host = SocksServ.host;
                port = SocksServ.port;
                to_socks_server = true;
            }
            else {
                host = npHost[hostIdx_].host;
                port = npHost[hostIdx_].port;
            }

            tcp::resolver::query q(host, port);
            resolver_.async_resolve(q,
                                    [this, self](boost::system::error_code ec, tcp::resolver::iterator itr){
                                        if (ec) cerr << "Error (async_resolve): " << ec.message() << "\n";
                                        else  do_connect(itr); });
        }

        void do_connect(tcp::resolver::iterator itr){
            cerr << "do_connect " << hostIdx_ << endl;
            auto self(shared_from_this());

            tcp::endpoint endpoint = *itr;

            socket_.async_connect(endpoint,
                                  [this, self](boost::system::error_code ec){
                                        if (ec) cerr << "Error (async_connect): " << ec.message() << "\n";

                                        else if(to_socks_server) {
                                            do_request();
                                        }

                                        else do_read();
                                    });
        }

        void do_request(){
            cerr << "do_request "<< hostIdx_  << endl;
            auto self(shared_from_this());

            memset(socks_request, '\0', max_length);
            socks_request[0] = 0x04;
            socks_request[1] = 0x01;
            socks_request[2] = (unsigned short)atoi((npHost[hostIdx_].port).c_str()) / 256;
            socks_request[3] = (unsigned short)atoi((npHost[hostIdx_].port).c_str()) % 256;
            socks_request[4] = 0x00;
            socks_request[5] = 0x00;
            socks_request[6] = 0x00;
            socks_request[7] = 0x01;
            memcpy((socks_request+9), (npHost[hostIdx_].host).c_str(), (npHost[hostIdx_].host).length());

            size_t len = 8+9+(npHost[hostIdx_].host).length()+1;

            async_write(socket_, boost::asio::buffer(socks_request, len),
                                     [this, self](boost::system::error_code ec, std::size_t) {
                                        if (ec)  cerr << "Error (async_write_some): " << ec.message() << "\n";
                                        else do_read_repy();
                                     });
        }

        void do_read_repy(){
            cerr << "do_read_repy "<< hostIdx_  << endl;

            auto self(shared_from_this());

            memset ( socks_reply, '\0' , sizeof(socks_reply) );
            //recive sock_reply
            socket_.async_read_some(boost::asio::buffer(socks_reply, 8),
                                    [this, self](boost::system::error_code ec, std::size_t length ) {
                                        if(ec) cerr << "Error (do_read): " << ec.message() << "\n";
                                        else if(socks_reply[1] == ACCEPT) do_read();
                                        else if(socks_reply[1] == REJECT) socket_.close();
                                    });
        }

        void handle_npHost_connect() {
            cerr << "handle_npHost_connect "<< hostIdx_  << endl;
            auto self(shared_from_this());
            [this, self]{ do_read(); };
        }

        void do_read() {
            cerr << "do_read "<< hostIdx_  << endl;
            auto self(shared_from_this());

            memset ( data_, '\0' , max_length );

            socket_.async_read_some(boost::asio::buffer(data_, max_length),
                                    [this, self](boost::system::error_code ec, std::size_t length) {

                                        if (ec == boost::asio::error::eof) {
                                            do_show(hostIdx_, data_);
                                            do_read();
                                        }
                                        else if(ec) cerr << "Error (do_read): " << ec.message() << "\n";
                                        else {
                                            do_show(hostIdx_, data_);
                                            if ( check_read_done(data_) )  do_write();
                                            if (is_exit == false ) do_read();
                                        } });
        }

        void do_show(int hostID, string context){
            cout << output_shell("s"+to_string(hostID), context);
        }

        void do_write() {
            auto self(shared_from_this());
            string cmd = "";
            getline(npHost[hostIdx_].openfile, cmd);
            cerr << "getline: "<< cmd << endl;
            cmd += "\n";
            socket_.async_write_some(boost::asio::buffer(cmd.c_str(), cmd.length()),
                                     [this, self](boost::system::error_code ec, std::size_t) {
                                        if (ec)  cerr << "Error (async_write_some): " << ec.message() << "\n";
                                        return;
                                     });
            cout << output_command("s"+to_string(hostIdx_), cmd);
            if (cmd=="exit\n") {
                is_exit = true;
                (npHost[hostIdx_].openfile).close();
            }
        }


};

int main(){
    int nHost = hostParse(getenv("QUERY_STRING"));
    cout << consoleCGI(nHost);

    try {
        for (int i=0; i<nHost; i++)  {
            make_shared<ConsoleSession>(i)->start();
        }
        global_io_service.run();
    }

    catch (std::exception& e) {
        cerr << "Exception: " << e.what() << "\n";
    }
}


int hostParse(string query_str){

    int nHost = 0;

    int n = 0;
    char* query_char = const_cast<char *>(query_str.c_str());

    vector<char*> cut_vec;
    char *cut = strtok(query_char, "&");
    while( cut != NULL ) {
        // cout << pch << endl;
        cut_vec.push_back(cut);
        n++;
        cut = strtok( NULL, "&");
    }
    for(int i=0; i<5; i++){
        npHost[i].hostID = i;
        npHost[i].host = getSubStrVal(cut_vec[i*3]);
        npHost[i].port = getSubStrVal(cut_vec[i*3+1]);
        npHost[i].file = getSubStrVal(cut_vec[i*3+2]);
    }



    for(int i=0; i<5; i++){
        if (npHost[i].host!="") nHost = i+1;
    }

    // npHost[i].openfile;
    for(int i=0; i<nHost; i++){
        char *filename = const_cast<char *>(("./test_case/"+npHost[i].file).c_str());
        (npHost[i].openfile).open(filename, ios::in);
    }

    //Socks server
    SocksServ.host = getSubStrVal(cut_vec[15]);
    SocksServ.port = getSubStrVal(cut_vec[16]);
    if(SocksServ.host!="") SocksServ.is_exist = true;
    else SocksServ.is_exist = false;
    // cout << "port= " << SocksServ.port << "ip= " << SocksServ.host << endl;

    return nHost;
}

string getSubStrVal(string ori){
    string seg1 = strtok(const_cast<char *>(ori.c_str()), "=");
    char *seg;
    if((seg = strtok(NULL, "="))!=NULL) return seg;
    else  return "";
}

string consoleCGI(int nHost){
    string hdr =  "Content-type: text/html\r\n\r\n";

    string HTML_th="", HTML_td="";

    for(int i=0; i<nHost; i++)  HTML_th += "          <th scope=\"col\">" + npHost[i].host+":"+npHost[i].port +  "</th>";
    for(int i=0; i<nHost; i++)  HTML_td += "          <td><pre id=\"s"+to_string(i)+"\" class=\"mb-0\"></pre></td>";

    string HTML_TEMPLATE_1 =
    "<!DOCTYPE html>"
    "<html lang=\"en\">"
    "  <head>"
    "    <meta charset=\"UTF-8\">"
    "    <title>NP Project 3 Console</title>"
    "    <link"
    "      rel=\"stylesheet\""
    "      href=\"https://cdn.jsdelivr.net/npm/bootstrap@4.5.3/dist/css/bootstrap.min.css\""
    "      integrity=\"sha384-TX8t27EcRE3e/ihU7zmQxVncDAy5uIKz4rEkgIXeMed4M0jlfIDPvg6uqKI2xXr2\""
    "      crossorigin=\"anonymous\""
    "    />"
    "    <link"
    "      href=\"https://fonts.googleapis.com/css?family=Source+Code+Pro\""
    "      rel=\"stylesheet\""
    "    />"
    "    <link"
    "      rel=\"icon\""
    "      type=\"image/png\""
    "      href=\"https://cdn0.iconfinder.com/data/icons/small-n-flat/24/678068-terminal-512.png\""
    "    />"
    "    <style>"
    "      * {"
    "        font-family: 'Source Code Pro', monospace;"
    "        font-size: 1rem !important;"
    "      }"
    "      body {"
    "        background-color: #212529;"
    "      }"
    "      pre {"
    "        color: #cccccc;"
    "      }"
    "      b {"
    "        color: #01b468;"
    "      }"
    "    </style>"
    "  </head>"
    "  <body>"
    "    <table class=\"table table-dark table-bordered\">"
    "      <thead>"
    "        <tr>";
            // <th scope=\"col\">nplinux1.cs.nctu.edu.tw:1234</th>
            // <th scope=\"col\">nplinux2.cs.nctu.edu.tw:5678</th>
    string HTML_TEMPLATE_2 =
    "        </tr>"
    "      </thead>"
    "      <tbody>"
    "        <tr>";
            // <td><pre id=\"s0\" class=\"mb-0\"></pre></td>
            // <td><pre id=\"s1\" class=\"mb-0\"></pre></td>
    string HTML_TEMPLATE_3 =
    "        </tr>"
    "      </tbody>"
    "    </table>"
    "  </body>"
    "</html>";

    string HTML = HTML_TEMPLATE_1 + HTML_th + HTML_TEMPLATE_2 + HTML_td + HTML_TEMPLATE_3;

    return hdr + HTML;

}


bool check_read_done(string oriStr){
    if( oriStr.find("% ") != string::npos ) return true;
    else return false;
}

string output_command(string host, string content){
    cerr << "output_command "<< host << " data " << content  << endl;
    content = replaceSubStr(content);
    string command = "<script>document.getElementById('"+ host +"').innerHTML += '<b>"+ content +"</b>';</script>";
    return command;
}

string output_shell(string host, string content){
    cerr << "output_shell "<< host << " data " << content  << endl;
    content = replaceSubStr(content);
    string command = "<script>document.getElementById('" + host + "').innerHTML += '" + content + "';</script>\n";
    return command;
}

string replaceSubStr(string oriStr){
    boost::replace_all(oriStr, "&", "&amp;");
    boost::replace_all(oriStr, "\"", "&quot;");
    boost::replace_all(oriStr, "\'", "&apos;");
    boost::replace_all(oriStr, "\r", "");
    boost::replace_all(oriStr, ">", "&gt;");
    boost::replace_all(oriStr, "<", "&lt;");
    boost::replace_all(oriStr, "\n", "&NewLine;");
    return oriStr;
}


