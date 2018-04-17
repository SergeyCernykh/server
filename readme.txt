Компиляция: c++ -g server.cpp -o server -std=c++11 -lcrypt
Тестировал с помощию culr: curl -0 -d '{"str": "str", "rounds": n}' 0.0.0.0:8000/hash

