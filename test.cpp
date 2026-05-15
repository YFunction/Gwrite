#include<iostream>

#include "include/hanzi.h"
#include "src/hanzi.cpp"

int main(){
    hanzi a;
    string word="向";
    a.printGCode(word,1000,-200,200,0.0,-2.0,10,1,30.0,true);

    return 0;
}