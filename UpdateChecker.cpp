// Базовые и общие библиотеки
#include <iostream>
#include <fstream>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <boost/algorithm/string/replace.hpp>

// Библиотеки для работы с потоками
#include <thread>
#include <chrono>
#include <mutex>

//Библиотеки для работы с Телеграм Ботом
#include <tgbot/tgbot.h>

// Библиотеки для работы с http
#include <curlpp/cURLpp.hpp>
#include <curlpp/Easy.hpp>
#include <curlpp/Options.hpp>

// Библиотеки для работы с PostgreSQL
#include <pqxx/pqxx>

// Объявление функции для проверки, есть ли уже такая запись в бизе данных и запись в базу данных
int putInfoInDatabase(int64_t userID, std::string url);

// Объявление функции для удаления из базы данных
bool deleteFromDatabase(int64_t userID, std::string url);

// ФОбъявление функции, отвечающую за основную работу бота в потоке
void botActivation(TgBot::Bot &bot);

// Объявление функции отвечающую за периодическую сверку HTML кодов в потоке
void HTML_Checker(TgBot::Bot& bot);

// Объявление функции для подсчета количества вхождений подстроки в строку
int countSubstrInString(std::string string, std::string substr);

// Мьютекс блокирует доступ к базе данных.
std::mutex db_locker;

int main()
{
    std::string token; // Токен телеграм бота
    std::ifstream token_file("token.txt"); // Файл с токеном
    if (token_file.is_open())
    {
        std::getline(token_file, token);
    }
    TgBot::Bot bot(token); // Токен телеграм-бота
    bot.getEvents().onCommand("start", [&bot](TgBot::Message::Ptr message) // Команда "/start"
        {
            bot.getApi().sendMessage(message->chat->id, "Hello! I am online!\nTo get my commands, write \"/help\".");
        });
    bot.getEvents().onCommand("add", [&bot](TgBot::Message::Ptr message) // Добавление URL в базу данных
        {
            std::string url = message->text.c_str();
            if (url.length() < 5) // Если в сообщении меньше 5 символов ("/add "), то не проверяет
            {
                bot.getApi().sendMessage(message->chat->id, "You didn\'t add an url to command.");
            }
            else
            { 
                url = url.substr(5, url.length() - 5); // Выборка url из сообщения

                int check = putInfoInDatabase(message->chat->id, url); // Попытка положить url и HTML в базу данных
                if (check == 0) // Успех
                {
                    bot.getApi().sendMessage(message->chat->id, "Your URL have been successfully added to database.");
                }
                else if (check == 1) // url уже в базе данных
                {
                    bot.getApi().sendMessage(message->chat->id, "Your URL is already contained in database.");
                }
                else // url нерабочий
                {
                    bot.getApi().sendMessage(message->chat->id, "Some error occured.");
                }
            }
        });
    bot.getEvents().onCommand("delete", [&bot](TgBot::Message::Ptr message) // Удаление url из базы данных
        {
            std::string url = message->text.c_str(); // Если в сообщении меньше 8 символов ("/delete "), то не проверяет
            if (url.length() < 8)
            {
                bot.getApi().sendMessage(message->chat->id, "You didn\'t add an url to command.");
            }
            else
            {
                url = url.substr(8, url.length() - 8); // Выборка url из сообщения

                if (deleteFromDatabase(message->chat->id, url)) // Если удаление прошло успешно
                {
                    bot.getApi().sendMessage(message->chat->id, "This URL have been successfully deleted from database.");
                }
                else // Если url нет в базе данных
                {
                    bot.getApi().sendMessage(message->chat->id, "This url isn\'t in the database.");
                }
            }
        });
    bot.getEvents().onCommand("help", [&bot](TgBot::Message::Ptr message) // Получение информации о работе с ботом
        {
            bot.getApi().sendMessage(message->chat->id, "Hello!I am \"Update Checker\" bot! You can call me Uc.\n"
            "Probavly there are some websites with newspapers, web novels or scientific articles you like to read a lot."
            " But there is no time for you to check if new article or chapter was added."
            "\nThat\'s why I exist - to check this sites for you and notify you if something was added!"
            "\nAlthought I am not perfect, I hope that I can help you and save your time!"
            "\nI check websites in my database once in 4 hours.\n These are commands that I know:"
            "\n\"/add <url>\" - add new website to my database;"
            "\n\"/delete <url>\" - delete website from my database;"
            "\n \"/help\" - to see info about me and all my commands again.");
        });
    bot.getEvents().onUnknownCommand([&bot](TgBot::Message::Ptr message) //На любое сообщение, бот будет реагировать предложением увидеть его команды.
        {
            bot.getApi().sendMessage(message->chat->id, "I would like to talk, but unfortunatly, I wasn\'t programmed for this.\nPrint \"/help\" to see my functions.");
        });
    bot.getEvents().onNonCommandMessage([&bot](TgBot::Message::Ptr message) //На любое сообщение, бот будет реагировать предложением увидеть его команды.
        {
            bot.getApi().sendMessage(message->chat->id, "I would like to talk, but unfortunatly, I wasn\'t programmed for this.\nPrint \"/help\" to see my functions.");
        });

    std::thread botThread(botActivation, std::ref(bot)); // Основной поток для работы телеграм-бота (1)
    std::thread checkThread(HTML_Checker, std::ref(bot)); // Поток для работы функции проверки обновлений по url из базы данных (2)
    botThread.join(); // Запуск потока 1
    checkThread.join(); // Запуск потока 2

    return 0;
}

// Функция для проверки, есть ли уже такая запись в бизе данных и запись в базу данных
int putInfoInDatabase(int64_t userID, std::string url)
{
    db_locker.lock(); // Юлокировка доступа к базе данных мьютексом
    std::string connection_description; // Описание подключения
    std::ifstream connection_file("database.txt"); // Открытие файла с описанием подключения
    if (connection_file.is_open())
    {
        std::getline(connection_file, connection_description);
    }
    pqxx::connection con{ connection_description }; // Подключение к базе данных
    pqxx::work tnx{ con };
     
    // Получение информации, записан ли для данного пользователя url в базу данных
    std::string query = "SELECT count(*) as counter FROM public.\"HTMLKeeper\" WHERE \"userID\" = " + std::to_string(userID);
    query += " AND url = \'" + url + "\'";
    int rows = tnx.query_value<int>(query);

    if (rows == 0) // Если url не записан
    {
        try // Попытка обратиться по url и получить HTML-код
        {
            std::ostringstream tempStream; // Буфер, в котором будет хранится HTML-код до записи в переменную
            tempStream << curlpp::options::Url(url); // Берет HTML код страницы и записывает его в буфер tempStream
            std::istringstream pipe(tempStream.str());
            getline(pipe, query, {}); // Запись HTML кода
            boost::replace_all(query, "\'", "\'\'"); //PostgreSQL воспринимает ' как символ завершения текста. Чтобы этого не случилось, меняем ' на ''
            query = "INSERT INTO public.\"HTMLKeeper\" VALUES ("
                + std::to_string(userID) + ", \'" + url + "\', \'" + query + "\')";
            tnx.exec0(query); // Выполнение операции
            tnx.commit(); // Сохранение изменений
            db_locker.unlock(); // Разблокировка мьютекса
            return 0; // Возвращение кода 0

        } //Если возникнет ошибка, разблокируем мьютекс и возвращаем код -1
        catch (curlpp::LogicError& e)
        {
            db_locker.unlock();
            return -1;
        }
        catch (curlpp::RuntimeError& e)
        {
            db_locker.unlock();
            return -1;
        }
    }
    // Если url уже был записан, разблокируем мьютекс и возвращаем код 1
    db_locker.unlock();
    return 1;
}

// Функция для удаления из базы данных
bool deleteFromDatabase(int64_t userID, std::string url)
{
    db_locker.lock(); // Блокировка мьютекса
    std::string connection_description; // Описание подключения
    std::ifstream connection_file("database.txt"); // Открытие файла с описанием подключения
    if (connection_file.is_open())
    {
        std::getline(connection_file, connection_description);
    }
    pqxx::connection con{ connection_description }; // Подключение к базе данных
    pqxx::work tnx{ con };

    // Запрос для проверки, есть ли такая строка в базе данных
    std::string query = "SELECT count(*) as counter FROM public.\"HTMLKeeper\" WHERE \"userID\" = " + std::to_string(userID);
    query += " AND url = \'" + url + "\'";
    int rows = tnx.query_value<int>(query);
    if (rows == 1) // Если есть - удаляем из базы данных
    {
        query = "DELETE FROM public.\"HTMLKeeper\" WHERE \"userID\" = " + std::to_string(userID);
        query += " AND url = \'" + url + "\'";
        tnx.exec(query); // Выполнение операции
        tnx.commit(); // Сохранение изменений
        db_locker.unlock(); // Разблокировка мьютекса
        return true; // Возвращение  true
    }
    // Если url не было, разблокируем мьютекс и возвращаем false
    db_locker.unlock();
    return false;
}

// Функция работы бота, выполняемая в потоке
void botActivation(TgBot::Bot &bot)
{
    try // Попытка подключиться к боту
    {
        printf("Bot username: %s\n", bot.getApi().getMe()->username.c_str());  // Если вышло - проводить обновление бота в цикле
        TgBot::TgLongPoll longPoll(bot);

        while (true) {
            longPoll.start();
        }
    }
    catch (TgBot::TgException& e) // Если не вышло - вывести ошибку
    {
        printf("error: %s\n", e.what());
    }
}

// Функция, отвечающая за периодическую сверку HTML кодов в потоке
void HTML_Checker(TgBot::Bot& bot)
{
    using namespace std::this_thread; // Области, нужные для функции "засыпания"
    using namespace std::chrono;
    while (true)
    {
        db_locker.lock(); // Блокировка мьютекса
        std::string connection_description; // Описание подключения
        std::ifstream connection_file("database.txt"); // Открытие файла с описанием подключения
        if (connection_file.is_open())
        {
            std::getline(connection_file, connection_description);
        }
        pqxx::connection con{ connection_description }; // Подключение к базе данных
        pqxx::work tnx{ con };
        std::string query = "SELECT * FROM public.\"HTMLKeeper\""; // Выборка всех данных из базы данных
        pqxx::result r = tnx.exec(query); // Выполнение операции и запись результата
        for (auto row : r) // Проход по всем строкам
        {
            std:int64_t userID = strtoll(row[0].c_str(), NULL, 10); // Получение ID пользователя из базы данных
            std::string url = row[1].c_str(); // Получение url-ссылки из базы данных
            std::string HTML_contained = row[2].c_str(); // Получение HTML-кода из базы данных
            boost::replace_all(HTML_contained, "\'\'", "\'"); // Замена всех '' на '

            std::string HTML;
            curlpp::Easy request;
            std::ostringstream tempStream;
            tempStream << curlpp::options::Url(url); // Берет HTML код страницы и записывает его в буфер tempStream
            std::istringstream pipe(tempStream.str());
            getline(pipe, HTML, {});

            // Подсчет количества полей "href" для кода актуального HTML и HTML из базы данных
            int hrefsInHTML = countSubstrInString(HTML, "href"), hrefsInHTML_contained = countSubstrInString(HTML_contained, "href");

            //Если количество отличается - было явное обновление
            if (hrefsInHTML != hrefsInHTML_contained)
            {
                std::string message = "Information on url \"" + url; // Строка, которая выступит в качестве сообщения о том,
                message += "\" changed! Check out!";                 // что по указанному url было обновление
                bot.getApi().sendMessage(userID, message);
                boost::replace_all(HTML, "\'", "\'\'"); // Замена всех ' на ''
                query = "UPDATE public.\"HTMLKeeper\" SET html = \'" + HTML + "\'" +
                    "WHERE \"userID\" = " + std::to_string(userID) +
                    " AND \"url\" = \'" + url + "\'";
                tnx.exec(query); // Обновление данных в базе данных
                tnx.commit();
            }
        }
        db_locker.unlock(); // Разблокировка мьютекса
        sleep_for(4h); // Функция засыпает на 4 часа
    }
}

// Функция для подсчета количества вхождений подстроки в строку
int countSubstrInString(std::string s, std::string subs)
{
    int occurences = 0;
    std::string::size_type pos = 0;
    while ((pos = s.find(subs, pos)) != std::string::npos) // Найдя вхождение подстроки в строку, смещает начало проверки и увеличивает
    {                                                      // счетчик на 1
        ++occurences;
        ++pos;
    }
    return occurences;
}