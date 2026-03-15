#include<iostream>
#include"include/hanzi.h"
#include"src/hanzi.cpp"

using namespace std;

int main(){
    hanzi a;
    a.printGCode("你好世界", 300.0, 0, 0, 55, 50, 2, 2, 60.0, true);
    return 0;
}