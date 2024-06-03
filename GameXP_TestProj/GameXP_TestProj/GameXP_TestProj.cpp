// GameXP_TestProj.cpp : Этот файл содержит функцию "main". Здесь начинается и заканчивается выполнение программы.
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

// Глобальная критическая секция для синхронизации доступа к векторам
CRITICAL_SECTION criticalSection;

// Класс с 64-битным полем и функцией сканирования файлов
class MyClass {
public:
    uint64_t number;

    MyClass(uint64_t num) : number(num) {}

    void scanAndLog(HANDLE stopEvent, std::vector<std::wstring>& txtFiles, std::vector<std::wstring>& accessedFiles) const {
        // Получаем текущий каталог
        wchar_t currentPath[MAX_PATH];
        DWORD pathLen = GetCurrentDirectoryW(MAX_PATH, currentPath);
        if (pathLen == 0 || pathLen > MAX_PATH) {
            std::wcerr << L"Failed to get current directory." << std::endl;
            return;
        }

        // Добавляем шаблон поиска файлов
        std::wstring searchPattern = std::wstring(currentPath) + L"\\*.txt";

        // Используем Windows API для поиска файлов с расширением *.txt
        WIN32_FIND_DATAW findFileData;
        HANDLE hFind = FindFirstFileW(searchPattern.c_str(), &findFileData);

        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                if (WaitForSingleObject(stopEvent, 0) == WAIT_OBJECT_0) {
                    FindClose(hFind);
                    return;  // Если событие установлено, прекращаем выполнение
                }

                // Проверяем на корректность переменную findFileData
                if (findFileData.dwFileAttributes != INVALID_FILE_ATTRIBUTES &&
                    !(findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                    // Добавляем имя файла в вектор
                    std::wstring fileName(findFileData.cFileName);

                    // Проверяем, обрабатывался ли файл другим потоком
                    if (std::find(accessedFiles.begin(), accessedFiles.end(), fileName) == accessedFiles.end()) {
                        EnterCriticalSection(&criticalSection);
                        txtFiles.push_back(fileName + L" | Thread ID: " + std::to_wstring(number)); // Записываем имя файла и значение поля number в лог
                        accessedFiles.push_back(fileName); // Помечаем файл как обработанный
                        LeaveCriticalSection(&criticalSection);

                        // Удаляем файл после записи в лог
                        std::wstring filePath = std::wstring(currentPath) + L"\\" + fileName;
                        if (!DeleteFileW(filePath.c_str())) {
                            std::wcerr << L"Failed to delete file: " << fileName << std::endl;
                        }
                    }
                }
            } while (FindNextFileW(hFind, &findFileData) != 0);
            FindClose(hFind);
        }

        // Устанавливаем событие по завершению работы
        SetEvent(stopEvent);
    }
};

// Функция, которая будет выполняться в потоке
DWORD WINAPI threadFunction(LPVOID param) {
    auto* params = static_cast<std::tuple<MyClass*, HANDLE, std::vector<std::wstring>*, std::vector<std::wstring>* >*>(param);
    MyClass* obj = std::get<0>(*params);
    HANDLE stopEvent = std::get<1>(*params);
    std::vector<std::wstring>* txtFiles = std::get<2>(*params);
    std::vector<std::wstring>* accessedFiles = std::get<3>(*params);
    obj->scanAndLog(stopEvent, *txtFiles, *accessedFiles);
    return 0;
}

// Функция генерации случайного 64-битного числа
uint64_t generateRandomNumber() {
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dis;
    return dis(gen);
}

int main() {
    // Инициализация критической секции
    InitializeCriticalSection(&criticalSection);

    std::cout << "Application is running. Press any key to stop." << std::endl;

    while (!_kbhit()) { // Пока не нажата клавиша
        // Генерация случайных 64-битных чисел
        uint64_t randomNum1 = generateRandomNumber();
        uint64_t randomNum2 = generateRandomNumber();

        // Создаем два объекта класса с переданными значениями
        MyClass obj1(randomNum1);
        MyClass obj2(randomNum2);

        // Создаем события для остановки потоков
        HANDLE stopEvent1 = CreateEvent(nullptr, TRUE, FALSE, nullptr);
        HANDLE stopEvent2 = CreateEvent(nullptr, TRUE, FALSE, nullptr);

        // Вектор для хранения имен файлов
        std::vector<std::wstring> txtFiles1;
        std::vector<std::wstring> txtFiles2;

        // Вектор для хранения имен файлов, к которым уже получен доступ
        std::vector<std::wstring> accessedFiles;

        // Создаем два потока
        std::tuple<MyClass*, HANDLE, std::vector<std::wstring>*, std::vector<std::wstring>* > param1 = { &obj1, stopEvent1, &txtFiles1, &accessedFiles };
        std::tuple<MyClass*, HANDLE, std::vector<std::wstring>*, std::vector<std::wstring>* > param2 = { &obj2, stopEvent2, &txtFiles2, &accessedFiles };

        HANDLE thread1 = CreateThread(
            nullptr,        // Дескриптор атрибута безопасности
            0,              // Начальный размер стека
            threadFunction, // Функция потока
            &param1,        // Аргумент для потока
            0,              // Флаги создания
            nullptr         // Идентификатор потока
        );

        HANDLE thread2 = CreateThread(
            nullptr,        // Дескриптор атрибута безопасности
            0,              // Начальный размер стека
            threadFunction, // Функция потока
            &param2,        // Аргумент для потока
            0,              // Флаги создания
            nullptr         // Идентификатор потока
        );

        // Ожидаем завершения первого потока
        HANDLE events[2] = { stopEvent1, stopEvent2 };
        DWORD waitResult = WaitForMultipleObjects(2, events, TRUE, INFINITE);

        // Запись найденных файлов в *.log файл
        std::wofstream logFile(L"files.log", std::ios_base::app);
        EnterCriticalSection(&criticalSection);

        if (waitResult == WAIT_OBJECT_0)
        {
            for (const auto& file : txtFiles1)
                logFile << file << std::endl;
        }
        else if (waitResult == WAIT_OBJECT_0 + 1)
        {
            for (const auto& file : txtFiles2)
                logFile << file << std::endl;
        }

        logFile << std::endl;

        LeaveCriticalSection(&criticalSection);

        // Закрываем потоки
        CloseHandle(thread1);
        CloseHandle(thread2);

        // Закрываем события
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