﻿// GameXP_TestProj.cpp : Этот файл содержит функцию "main". Здесь начинается и заканчивается выполнение программы.
//

#include "pch.h"
#include <iostream>
#include <vector>
#include <string>
#include <random>
#include <fstream>
#include <windows.h>
#include <tuple>
#include <conio.h>
#include <locale>
#include <codecvt>

// Глобальная критическая секция для синхронизации доступа к векторам
CRITICAL_SECTION criticalSection;

// Класс с 64-битным полем и функцией сканирования файлов
class ThreadClass 
{
public:
    uint64_t ThreadID;

    ThreadClass(uint64_t num) : ThreadID(num) {}

    void scanAndLog(HANDLE stopEvent, std::vector<std::wstring>& txtFiles, std::vector<std::wstring>& accessedFiles) const 
    {
        // Получаем текущий каталог
        wchar_t currentPath[MAX_PATH];
        DWORD pathLen = GetCurrentDirectoryW(MAX_PATH, currentPath);

        if (pathLen == 0 || pathLen > MAX_PATH) 
        {
            std::wcerr << L"Failed to get current directory." << std::endl;
            return;
        }

        // Добавляем шаблон поиска файлов
        // Используем Windows API для поиска файлов с расширением *.txt
        std::wstring searchPattern = std::wstring(currentPath) + L"\\*.txt";
        WIN32_FIND_DATAW findFileData;
        HANDLE hFind = FindFirstFileW(searchPattern.c_str(), &findFileData);

        if (hFind != INVALID_HANDLE_VALUE) 
        {
            do 
            {
                if (WaitForSingleObject(stopEvent, 0) == WAIT_OBJECT_0) 
                {
                    FindClose(hFind);
                    return;  // Если событие установлено, прекращаем выполнение
                }

                std::wstring logFileName = L"files";

                // Проверяем на корректность переменную findFileData
                if (findFileData.dwFileAttributes != INVALID_FILE_ATTRIBUTES &&
                    !(findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) 
                {
                    // Добавляем имя файла в вектор
                    std::wstring fileName(findFileData.cFileName);

                    // Проверяем, обрабатывался ли файл другим потоком
                    if (std::find(accessedFiles.begin(), accessedFiles.end(), fileName) == accessedFiles.end() &&
                        fileName != logFileName)
                    {
                        EnterCriticalSection(&criticalSection);

                        txtFiles.push_back(fileName + L" | Thread ID: " + std::to_wstring(ThreadID)); // Записываем имя файла и идентификатор нашедшего его треда в лог-вектор
                        accessedFiles.push_back(fileName); // Помечаем файл как обработанный

                        LeaveCriticalSection(&criticalSection);

                        // Удаляем файл после записи в лог
                        std::wstring filePath = std::wstring(currentPath) + L"\\" + fileName;

                        if (!DeleteFileW(filePath.c_str())) 
                        {
                            std::wcerr << L"Failed to delete file: " << fileName << std::endl;
                        }
                    }
                }
            } 
            while (FindNextFileW(hFind, &findFileData) != 0);

            FindClose(hFind);
        }

        // Устанавливаем событие по завершению работы
        SetEvent(stopEvent);
    }
};

// Функция, которая будет выполняться в потоке
DWORD WINAPI threadFunction(LPVOID param) 
{
    auto* params = static_cast<std::tuple<ThreadClass*, HANDLE, std::vector<std::wstring>*, std::vector<std::wstring>* >*>(param);

    ThreadClass* obj = std::get<0>(*params);
    HANDLE stopEvent = std::get<1>(*params);
    std::vector<std::wstring>* txtFiles = std::get<2>(*params);
    std::vector<std::wstring>* accessedFiles = std::get<3>(*params);

    obj->scanAndLog(stopEvent, *txtFiles, *accessedFiles);

    return 0;
}

uint64_t generateRandomThreadID() 
{
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dis;
    return dis(gen);
}

int main() 
{
    // Настроим локали, что бы в итоговом логе так же отображались файлы с кириллическими символами в названии(вдруг пригодится)
    std::locale::global(std::locale("ru_RU.utf8"));
    std::wcout.imbue(std::locale());
    std::wcin.imbue(std::locale());

    // Инициализация критической секции
    InitializeCriticalSection(&criticalSection);

    std::cout << "Application is running. Press any key to stop." << std::endl;

    while (!_kbhit()) // Пока не нажата клавиша
    { 
        ThreadClass obj1(generateRandomThreadID());
        ThreadClass obj2(generateRandomThreadID());

        HANDLE stopEvent1 = CreateEvent(nullptr, TRUE, FALSE, nullptr);
        HANDLE stopEvent2 = CreateEvent(nullptr, TRUE, FALSE, nullptr);

        // Вектора для записи имен найденных файлов на каждый поток
        std::vector<std::wstring> txtFiles1;
        std::vector<std::wstring> txtFiles2;

        // Вектор для хранения имен файлов, к которым уже получен доступ в любом из потоков
        std::vector<std::wstring> accessedFiles;

        // Пакуем параметры для экземпляров потока
        std::tuple<ThreadClass*, HANDLE, std::vector<std::wstring>*, std::vector<std::wstring>* > param1 = { &obj1, stopEvent1, &txtFiles1, &accessedFiles };
        std::tuple<ThreadClass*, HANDLE, std::vector<std::wstring>*, std::vector<std::wstring>* > param2 = { &obj2, stopEvent2, &txtFiles2, &accessedFiles };

        // Создаем потоки с заранее заготовленными параметрами
        HANDLE thread1 = CreateThread(nullptr, 0, threadFunction, &param1, 0, nullptr);
        HANDLE thread2 = CreateThread(nullptr, 0, threadFunction, &param2, 0, nullptr);

        // Ожидаем завершения самого быстрого потока
        HANDLE events[2] = { stopEvent1, stopEvent2 };
        DWORD waitResult = WaitForMultipleObjects(2, events, TRUE, INFINITE);

        std::wofstream logFile(L"files.log", std::ios_base::app); //
                                                                  //
        EnterCriticalSection(&criticalSection);                   //
                                                                  //
        if (waitResult == WAIT_OBJECT_0)                          //
        {                                                         //
            for (const auto& file : txtFiles1)                    //
                logFile << file << std::endl;                     // Запись найденных файлов из самого быстрого потока в *.log файл
        }                                                         //
        else if (waitResult == WAIT_OBJECT_0 + 1)                 //
        {                                                         //
            for (const auto& file : txtFiles2)                    //
                logFile << file << std::endl;                     //
        }                                                         //
                                                                  //
        logFile << std::endl;                                     //
                                                                  //
        LeaveCriticalSection(&criticalSection);                   //

        // Закрываем потоки и события
        CloseHandle(thread1);
        CloseHandle(thread2);
        CloseHandle(stopEvent1);
        CloseHandle(stopEvent2);

        // Задержка перед новой итерацией от 2 до 4 секунд
        Sleep((rand() % 3 + 2) * 1000);
    }

    // Очищаем критическую секцию
    DeleteCriticalSection(&criticalSection);

    // Завершаем работу программы
    return 0;
}