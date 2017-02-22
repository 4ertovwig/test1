#ifndef CONNECT_HPP
#define CONNECT_HPP

//#define BOOST_COROUTINE_NO_DEPRECATION_WARNING
//#define BOOST_ASIO_ENABLE_BUFFER_DEBUGGING

#include <iostream>
#include <fstream>
#include <thread>
#include <mutex>
#include <regex>

#include <boost/asio.hpp>
#include <boost/coroutine/all.hpp>
#include <boost/bind.hpp>

namespace http_client {

using boost::coroutines::coroutine;

class connect_base {

protected:

    enum class code {
        INFORMATIONAL,   //информационный ответ, ничего серверу больше не отправляем
        OK,              //страница получена
        REDIRECTION,     //перенаправление, ищем Location
        CLIENT_ERROR,    //наша ошибка
        SERVER_ERROR     //совсем печальный ответ
    };

public:

    /*
    * реализация поиска ответа в лоб т.е. по первой цифре
    */
    code find_code(std::string& in) {

        std::vector<std::string> temp;
        std::istringstream iss(in);

        std::copy(std::istream_iterator<std::string>{iss},
                std::istream_iterator<std::string>{},
                std::back_inserter(temp));

        switch(temp[1][0]) {
          case '2' : return code::OK;
          case '3' : return code::REDIRECTION;
          case '4' : return code::CLIENT_ERROR;
          case '5' : return code::SERVER_ERROR;
          default  : return code::INFORMATIONAL;
        }
    }

    /*
     * достаточно унылая реализация парсинга ответа
    */
    void find_parameters(std::istream& iss) {

        std::string line;
        while (std::getline(iss, line))
        {
            if(line.substr(0, 8) == "Location") {
                std::string protocol;

                address.clear();
                parameter.clear();
                port.clear();

                std::regex r(R"(Location:\s+([a-z]+)://(?:www\.)?([^/]+)/?([^?]*)(?:\?(.*))?\r)");
                std::smatch matches;
                if(std::regex_match(line, matches, r)) {
                    protocol = matches.str(1);
                    if(protocol == "http")
                        port = "80";
                    else port = "443";
                    if(!line.find("www."))
                        address = matches.str(2);
                    else {
                        address += "www.";
                        address += matches.str(2);
                    }
                    std::string temp = matches.str(3);
                    if(!temp.empty()) {
                    //    address += "/";
                    //    address += matches.str(3);
                        temp = "/";
                    }
                    parameter = matches.str(4);
                    if(!parameter.empty())
                        //parameter = "/?" + parameter;
                        parameter = temp + "?" + parameter;
                    else parameter = "/";
                    return;
                }
            }
        }
    }

protected:

    connect_base( const std::string& address_,
                  const std::string& parameter_,
                  const std::string& port_)
        : address(address_),
          parameter(parameter_),
          port(port_)
    {
    }

    /*
     * короткая форма вывода в стрим
    */
    template<typename...Arg>
    inline void out(std::ostream& os, Arg&&...args) {
        int dummy[sizeof...(Arg)] = {
            ((os << args), 0)...
        };
        (void)dummy;
    }

    //адрес хоста
    std::string address;
    //передающийся параметр
    std::string parameter;
    //порт на который отправляем
    std::string port;
};


/*
 * класс асинхронных запросов
*/
class asynchronous_connect :
        public connect_base
        //public std::enable_shared_from_this<asynchronous_connect>
{

public:

    using ptr =  std::shared_ptr<asynchronous_connect>;

    /*
     * конструируем указатель
    */
    template<typename...Arg>
    static ptr create(Arg&&...args) {
        //return ptr(new asynchronous_connect(std::forward<Arg>(args)...));
	return std::make_shared<asynchronous_connect>(std::forward<Arg>(args)...);
    }

    asynchronous_connect(
             bool download_file_,
             boost::asio::io_service& service,
             const std::string& address_,
             const std::string& parameter_,
             std::mutex& synch,
             std::ostream& os_ = std::cout
            )
        : connect_base(address_, parameter_, "80"),
          download_file(download_file_),
          state(code::REDIRECTION),
          os_synchronization(synch),
          os(os_),
          request(),
          response_buffer(),
          resolver(service),
          socket(service)

    {
        reconnect();
    }

private:

    /*
     * начинаем соединение либо перезапрашиваем ответ
    */
    void reconnect();

    /*
     * подключаемся к хосту
    */
    void connect(const boost::system::error_code& err);

    /*
     * читаем ответ от хоста
    */
    void write_request(const boost::system::error_code& err);

    /*
    * читаем заголовок и в зависимости от заголовка дальше действуем
    */
    void read_headers(const boost::system::error_code& err);

    /*
     * докачиваем тело ответа
    */
    void read_content( const boost::system::error_code& err );



    //флаг сигнализирующий о режиме вывода ответа
    bool download_file;
    //состояние соединения
    code state;
    //ссылка на мьютекс которым синхронизируется вывод в стрим
    std::mutex& os_synchronization;
    //стрим в который нужно выводить ответ
    std::ostream& os;

    boost::asio::ip::tcp::resolver resolver;
    boost::asio::streambuf request;
    boost::asio::streambuf response_buffer;
    boost::asio::ip::tcp::socket socket;

};


/*
 * класс псевдоасинхронных запросов реализованный через coroutines
*/
class pseudo_asynchronous_connect : public connect_base
{

public:

    pseudo_asynchronous_connect(
                         coroutine<void>::pull_type& external,
			 bool download_file_,
                         boost::asio::io_service& service,
                         const std::string& address_,
                         const std::string& parameter_,
                         std::ostream& os_ )
        : connect_base(address_, parameter_, "80"),
	  download_file(download_file_),
          os(os_),
          resolver(service),
          socket(service)
    {
        reconnect(external);
    }

private:

    /*
     * начинаем соединение либо перезапрашиваем ответ
    */
    void reconnect(boost::coroutines::coroutine<void>::pull_type& external);

    /*
     * основной цикл крутящийся либо пока не вернется 200 либо ошибка
    */
    void request_loop(boost::coroutines::coroutine<void>::pull_type& external,
                      code& state);

    //флаг сигнализирующий о режиме вывода ответа
    bool download_file;
    //стрим в который нужно выводить ответ
    std::ostream& os;

    boost::asio::ip::tcp::resolver resolver;
    boost::asio::ip::tcp::socket socket;
};

/*
 * непосредственно сам класс клиента
 * по названиям функций понятно что они делают
*/
class client : boost::noncopyable {

    using deque = std::deque<std::shared_ptr<asynchronous_connect>>;

public:

    void GetHtmlPageAsync( boost::asio::io_service& service,
                           const std::string& address,
                           const std::string& parameter,
                           std::ostream& os = std::cout )
    {
        asynchronous_connect::ptr connect
                = asynchronous_connect::create(false,
                                               service,
                                               address,
                                               parameter,
                                               os_synchronization,
                                               os);
        deq.push_back(connect);

    }

    //==========================================================

    void GetHtmlPagePseudoAsync( coroutine<void>::pull_type& cor,
                                 boost::asio::io_service& service,
                                 const std::string& address,
                                 const std::string& parameter,
                                 std::ostream& os = std::cout )
    {
        pseudo_asynchronous_connect
                connect(cor,
			false,
                        service,
                        address,
                        parameter,
                        os);
    }

    //==========================================================

    void DownloadFileAsync(boost::asio::io_service& service,
                           const std::string& address,
                           const std::string& parameter,
                           std::ostream& os)
    {
        asynchronous_connect::ptr connect
                = asynchronous_connect::create(true,
                                               service,
                                               address,
                                               parameter,
                                               os_synchronization,
                                               os);
        deq.push_back(connect);
    }

    //==========================================================

    void DownloadFilePseudoAsync( coroutine<void>::pull_type& cor,
                                  boost::asio::io_service& service,
                                  const std::string& address,
                                  const std::string& parameter,
                                  std::ostream& os = std::cout )
    {
        pseudo_asynchronous_connect
                connect(cor,
			true,
                        service,
                        address,
                        parameter,
                        os);
    }

private:
    //мьютекс которым синхронизируем вывод в консоль
    std::mutex os_synchronization;
    //очередь для добавления туда указателя на
    deque deq;
};

}   // namespace http_client

#endif // CONNECT_HPP
