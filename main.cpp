#include <./connects.hpp>

int main(int argc, char *argv[]) {
	
	{
		//демонстрация работы асинхронных методов
		using http_client::client;
		boost::asio::io_service io1;
		boost::asio::io_service io2;
		boost::asio::io_service io3;
		boost::asio::io_service io4;
		http_client::client c;
		std::ofstream os("mpl.pdf");


		c.GetHtmlPageAsync(io1, "google.ru", "/");
		c.GetHtmlPageAsync(io2, "yandex.ru", "/");
		c.GetHtmlPageAsync(io3, "mail.ru", "/");
		c.DownloadFileAsync(io4, "boost.org", "/?doc/libs/1_62_0/libs/mpl/doc/paper/mpl_paper.pdf", os);
		
		std::thread trd1([&]{ io1.run();});
		std::thread trd2([&]{ io2.run();});
		std::thread trd3([&]{ io3.run();});
                std::thread trd4([&]{ io4.run();});
						
		trd1.join();
		trd2.join();			
		trd3.join();                                                          
		trd4.join();
	}
	{
		//демонстрация работы клиента через coroutines
		using http_client::client;
		using boost::coroutines::coroutine;
	        boost::asio::io_service io1;
		client c;
		std::ofstream os("boost");
		std::ofstream oss("cookbook.pdf");

		coroutine<void>::push_type coroutine1(std::bind(&client::GetHtmlPagePseudoAsync,			                                      
						      &c,
	                                              std::placeholders::_1,
						      std::ref(io1),
						      "boost.org",
						      "/",
						      std::ref(os)));
		coroutine<void>::push_type coroutine2(std::bind(&client::GetHtmlPagePseudoAsync,
						      &c,									                              
						      std::placeholders::_1,
						      std::ref(io1),
						      "ebooksbucket.com",
						      "/uploads/itprogramming/cplus/Boost_Cplusplus_Application_Development_Cookbook.pdf",													    std::ref(oss)));
						      
		while (coroutine1 || coroutine2) {									   
			if(coroutine1)	
				coroutine1();																					      
			if(coroutine2)
				coroutine2(); 
		}
	}
	return 0;
}
