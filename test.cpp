#define DEBUG

#include"include/PreDEBUG.h"
#include<iostream>
#include<cstring>
#include"include/hanzi.h"
#include"src/hanzi.cpp"

using namespace std;

int main(){
    string write;
    cin>>write;
    hanzi a(1);
    a.printGCode(write, 1500.0, -120, 250, 0, -4, 5, 1, 50.0, false);
    //a.printSingleWord(write);
    //a.printGCode(write, 3000.0, 0, 0, 5, -5, 4, 1, 60.0, false);
    return 0;
}