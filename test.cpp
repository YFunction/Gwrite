//#define DEBUG

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
    a.printGCode(write, 1000.0, -190, 200, 0, -2, 18, 1, 30.0, false);
    //a.printSingleWord(write);
    //a.printGCode(write, 3000.0, 0, 0, 5, -5, 4, 1, 60.0, false);
    return 0;
}
/*
目测函数传参调整至此
并将初始高度设置为恰好使得笔尖接触纸面并下降1-2mm
效果最好
*/