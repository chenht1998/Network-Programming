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

class SERVER {
    public:
        string host;
        string port;
        string file;

        fstream openfile;
        int hostID;
};

SERVER npHost[5];
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
            tcp::resolver::query q(npHost[hostIdx_].host, npHost[hostIdx_].port);
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
                                        else  do_read(); });
                                //   boost::bind(&ConsoleSession::do_read, this));
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

        void do_show(int hostID, string context){
            cout << output_shell("s"+to_string(hostID), context);
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
    for(int i=0; i<(n/3); i++){
        npHost[i].hostID = i;
        npHost[i].host = getSubStrVal(cut_vec[i*3]);
        npHost[i].port = getSubStrVal(cut_vec[i*3+1]);
        npHost[i].file = getSubStrVal(cut_vec[i*3+2]);
    }

    for(int i=0; i<(n/3); i++){
        if (npHost[i].host!="") nHost = i+1;
    }

    // npHost[i].openfile;
    for(int i=0; i<nHost; i++){
        char *filename = const_cast<char *>(("./test_case/"+npHost[i].file).c_str());
        (npHost[i].openfile).open(filename, ios::in);
    }
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


