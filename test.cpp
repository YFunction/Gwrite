#define DEBUG

#include"include/PreDEBUG.h"
#include<iostream>
#include<cstring>
#include"include/hanzi.h"
#include"src/hanzi.cpp"


using namespace std;

int main(){
    system("chcp 65001");
    string write;
    cin>>write;
    hanzi a(1);
    a.printGCode(write, 1000.0, -50, 200, 0, -2, 18, 1, 15.0, false);
    //a.printSingleWord(write);
    //a.printGCode(write, 3000.0, 0, 0, 5, -5, 4, 1, 60.0, false);
    return 0;
}