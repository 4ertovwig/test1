#include <connects.hpp>

namespace http_client {

//============================================================================

void asynchronous_connect::reconnect() {

    // попытка очистить буферы
    request.commit(request.size());
    std::istream is_req(&request);
    request.consume(request.size());

    response_buffer.commit(response_buffer.size());
    std::istream is_res(&response_buffer);
    response_buffer.consume(response_buffer.size());


    std::ostream request_stream(&request);
    out(request_stream,
        "GET ", parameter, " HTTP/1.1\r\n",
        "HOST: ", address, "\r\n",
        "Accept: */*\r\n",
        "Connection: close\r\n\r\n");

    boost::asio::ip::tcp::resolver::query query(address, port);
    boost::asio::ip::tcp::resolver::iterator endpoint_iterator = resolver.resolve(query);

    //начинаем всю асинхронность
    resolver.async_resolve(query,
        /*boost::bind( &asynchronous_connect::handle_resolve,
                     this,
                     boost::asio::placeholders::error,
                     endpoint_iterator) */
                           [&]( const boost::system::error_code& err,
                                boost::asio::ip::tcp::resolver::iterator endpoint_iterator )
                            {
                                if (!err) {
                                    boost::asio::async_connect(socket, endpoint_iterator,
                                        boost::bind(&asynchronous_connect::connect,
                                                    this,
                                                    boost::asio::placeholders::error));
                                  }
                                  else out(std::cout, "ошибка при чтении адреса : ", err.message());
                           });
}

//============================================================================

void asynchronous_connect::connect(const boost::system::error_code& err)
{
    if (!err) {
      // если соединение нормальное, то посылаем запрос
      boost::asio::async_write(socket, request,
          boost::bind(&asynchronous_connect::write_request,
                      //shared_from_this(),
                      this,
                      boost::asio::placeholders::error));
    }
    else out(std::cout, "ошибка при соединении :", err.message());
}

//============================================================================

void asynchronous_connect::write_request(const boost::system::error_code& err)
{
    if (!err) {
        boost::asio::async_read_until(socket, response_buffer, "\r\n\r\n",
            boost::bind(&asynchronous_connect::read_headers,
                        //shared_from_this(),
                        this,
                        boost::asio::placeholders::error));
    }
    else out(std::cout, "ошибка отправки по сокету :", err.message());
}

//============================================================================

void asynchronous_connect::read_headers(const boost::system::error_code& err)
{
    if (!err) {

        auto data = response_buffer.data();
        if(!download_file) {
            {
                std::unique_lock<std::mutex> lock(os_synchronization);
                // если получаем страницу, то ответ тоже выводим в стрим
                std::copy(boost::asio::buffers_begin(data),
                          boost::asio::buffers_end(data),
                          std::ostream_iterator<char>{os,""});
            }
        } else {
            {
                std::unique_lock<std::mutex> lock(os_synchronization);
                // если скачиваем файл, то ответ посылаем в консоль а не в стрим
                std::copy(boost::asio::buffers_begin(data),
                          boost::asio::buffers_end(data),
                          std::ostream_iterator<char>{std::cout,""});
            }
        }

        std::istream response_stream(&response_buffer);
        std::string code_response;
        std::getline(response_stream, code_response);
        state = find_code(code_response);

        switch(state) {
            case code::INFORMATIONAL :
            case code::CLIENT_ERROR  :
            case code::SERVER_ERROR  : break;
            //позволяем редирект
            case code::REDIRECTION   :  find_parameters(response_stream);
                                        reconnect();
            //выводим в стрим html-код страницы
            case code::OK            :
                                boost::asio::async_read(socket, response_buffer,
                                    boost::asio::transfer_at_least(1),
                                    boost::bind(&asynchronous_connect::read_content,
                                                //shared_from_this(),
                                                this,
                                                boost::asio::placeholders::error));

        }

    }
    else out(std::cout, "ошибка при чтении заголовка :", err.message());
}

//============================================================================

void asynchronous_connect::read_content( const boost::system::error_code& err )
{
    if (!err)
    {
        {
            std::unique_lock<std::mutex> lock(os_synchronization);
            out(os, &response_buffer);
        }

        boost::asio::async_read(socket, response_buffer,
            boost::asio::transfer_at_least(1),
            boost::bind(&asynchronous_connect::read_content,
                        //shared_from_this(),
                        this,
                        boost::asio::placeholders::error));
    }
    else if (err != boost::asio::error::eof)
        out(std::cout, "ошибка при чтении тела ответа :", err.message());
}

//============================================================================

void pseudo_asynchronous_connect::reconnect(boost::coroutines::coroutine<void>::pull_type& external)
{
    /// пробуем всегда сначала на http
    /// если домен на https, то далее посылаем на https
    code state = code::REDIRECTION;

    request_loop(external,
                 state);
}

//============================================================================

void pseudo_asynchronous_connect::request_loop(boost::coroutines::coroutine<void>::pull_type& external,
                  code& state)
{

    while( state != code::OK ) {

        /// коннектимся
        boost::asio::ip::tcp::resolver::query query(address, port);
        boost::asio::ip::tcp::resolver::iterator endpoint_iterator = resolver.resolve(query);

        boost::asio::connect(socket, endpoint_iterator);

        /// формируем запрос
        boost::asio::streambuf request;
        std::ostream request_stream(&request);
        out(request_stream,
            "GET ", parameter, " HTTP/1.1\r\n",
            "HOST: ",address, "\r\n",
            "Accept: */*\r\n",
            "Connection: close\r\n\r\n");

        /// отправляем запрос
        boost::asio::write(socket, request);

        /// читаем заголовок ответа
        boost::asio::streambuf response_buffer;
        boost::asio::read_until(socket, response_buffer, "\r\n\r\n");

        //прыгаем из сопрограммы
        external();

        // выводим заголовок ответа в стрим
        auto data = response_buffer.data();
	if(!download_file) {
            std::copy(boost::asio::buffers_begin(data),
                      boost::asio::buffers_end(data),
                      std::ostream_iterator<char>{os,""});
	} else {
	    std::copy(boost::asio::buffers_begin(data),
	              boost::asio::buffers_end(data),
		      std::ostream_iterator<char>{std::cout, ""});
	}

        //прыгаем после распечатывания заголовка
        external();

        /// узнаем код ответа
        std::istream response_stream(&response_buffer);
        std::string code_response;
        std::getline(response_stream, code_response);

        state = find_code(code_response);

        switch(state) {
            case code::INFORMATIONAL :
            case code::CLIENT_ERROR  :
            case code::SERVER_ERROR  : break;
            //позволяем редирект
            case code::REDIRECTION   :  find_parameters(response_stream);
                                        external(); continue;
            //выводим в стрим html-код страницы
            case code::OK            :  boost::system::error_code error;
                                        while (boost::asio::read(socket, response_buffer,
                                              boost::asio::transfer_at_least(1), error))
                                            out(os, &response_buffer);
                                        if (error != boost::asio::error::eof)
                                            throw boost::system::system_error(error);
        }

    }

    external();
}


}   //namespace http_client
