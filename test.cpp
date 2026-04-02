#include<iostream>
#include<cstring>
#include"include/hanzi.h"
#include"src/hanzi.cpp"

using namespace std;

int main(){
    string write;
    cin>>write;
    hanzi a;
    a.printGCode(write, 3000.0, -40, 200, 0, -10, 4, 1, 60.0, false);
    //a.printGCode(write, 3000.0, 0, 0, 5, -5, 4, 1, 60.0, false);
    return 0;
}
/*
z_up z_down参数调整好了，对应笔按下和抬起（非完全抬起）在这个范围内
1.多个字超限
2.部分卡顿
3.自适应高度
4.起笔高度分离

竖直下笔是圆头，行进间抬笔是尖头

*/