// ConsoleApplication1.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include <iostream>
#include "p0_starter.h"

int main()
{
    int row = 5;
    int col = 5;
    std::vector<int> numbers(25, 1);//初始化容器，25个，全为1
    scudb::RowMatrix<int>* m = new scudb::RowMatrix<int>(row, col);
    scudb::RowMatrix<int>* m1 = new scudb::RowMatrix<int>(row, col);
    scudb::RowMatrix<int>* m2 = new scudb::RowMatrix<int>(row, col);
    m->FillFrom(numbers);//形成3个5*5，且元素为1的矩阵
    m1->FillFrom(numbers);
    m2->FillFrom(numbers);
    scudb::RowMatrixOperations<int> op;
    std::unique_ptr<scudb::RowMatrix<int>> m3=op.Add(m1, m2);
    std::cout << "加法"<<std::endl;
    for (int j = 0; j < row; j++) 
    {
        for (int i = 0; i < col; i++)
        {
            std::cout << m3->GetElement(j, i) << " ";
        }
        std::cout << std::endl;
    }
    std::cout << std::endl;
    std::unique_ptr<scudb::RowMatrix<int>> m4 = op.Multiply(m1, m2);
    std::cout << "乘法" << std::endl;
    for (int j = 0; j < row; j++)
    {
        for (int i = 0; i < col; i++)
        {
            std::cout << m4->GetElement(j, i) << " ";
        }
        std::cout << std::endl;
    }
    std::cout << std::endl;
    std::unique_ptr<scudb::RowMatrix<int>> m5 = op.GEMM(m1, m2,m);
    std::cout << "加法与乘法" << std::endl;
    for (int j = 0; j < row; j++)
    {
        for (int i = 0; i < col; i++)
        {
            std::cout << m5->GetElement(j, i) << " ";
        }
        std::cout << std::endl;
    }
    

    return 0;  

}

// 运行程序: Ctrl + F5 或调试 >“开始执行(不调试)”菜单
// 调试程序: F5 或调试 >“开始调试”菜单

// 入门使用技巧: 
//   1. 使用解决方案资源管理器窗口添加/管理文件
//   2. 使用团队资源管理器窗口连接到源代码管理
//   3. 使用输出窗口查看生成输出和其他消息
//   4. 使用错误列表窗口查看错误
//   5. 转到“项目”>“添加新项”以创建新的代码文件，或转到“项目”>“添加现有项”以将现有代码文件添加到项目
//   6. 将来，若要再次打开此项目，请转到“文件”>“打开”>“项目”并选择 .sln 文件
