#include <Windows.h>
#include <process.h>
#include <string>
#include <vector>
#include <mmsystem.h>
#include <opencv.hpp>
#include <fstream>
#include <iomanip>

#pragma comment(lib, "winmm.lib") 

using namespace std;
using namespace cv;

namespace config {
    wstring WindowClassName;
    wstring WindowTitle;
    wstring ChildClassName;
    Vec3b colorEdge;
    Vec3b colorDark;
    Vec3b colorBright;
    Vec3b colorGrid;
    Vec3b colorFrame;
    bool DrawGrid;

    vector<wstring> split(wstring str, char delimiters) {
        vector<wstring> res;
        for (size_t l = 0, r = 0; r <= str.size(); ++r) {
            if (r == str.size() || str[r] == delimiters) {
                res.push_back(str.substr(l, r - l));
                l = r + 1;
            }
        }
        return res;
    }

    void Parse(wstring line) { //解析一行 config
        if (line.empty() || line[0] == L'#') //跳过注释
            return;
        wstring key, value;
        vector<wstring> lsp = split(line, '=');
        if (lsp.size() < 2)
            return;
        key = lsp[0].substr(lsp[0].find_first_not_of(' '), lsp[0].find_last_not_of(' ') + 1);
        value = lsp[1].substr(lsp[1].find_first_not_of(' '), lsp[1].find_last_not_of(' ') + 1);
        if (value.size() > 1 && value.front() == '"' && value.back() == '"')
            value = value.substr(1, value.size() - 2);

        if (key == L"WindowClassName")
            WindowClassName = value;
        else if (key == L"WindowTitle")
            WindowTitle = value;
        else if (key == L"ChildClassName")
            ChildClassName = value;
        else if (key == L"DrawGrid")
            DrawGrid = value == L"true";
        else if (key.substr(0, 5) == L"Color") {
            Vec3b color;
            for (int i = 0; i < 3; ++i)
                color[i] = _wtoi(split(value, ',')[i].c_str());
            if (key == L"ColorEdge")
                colorEdge = color;
            if (key == L"ColorDark")
                colorDark = color;
            if (key == L"ColorBright")
                colorBright = color;
            if (key == L"ColorGrid")
                colorGrid = color;
            if (key == L"ColorFrame")
                colorFrame = color;
        }
        else {
            wcout << L"Unknown config parameter: " << key << endl;
        }
    }

    void ReadConfig() {
        wfstream fs(L"./config.cfg", ios::in);
        if (fs.is_open()) {
            wstring line;
            for (; getline(fs, line);) {
                Parse(line);
            }
        }
    }
};

HWND EnumHWnd = 0;   // 用来保存CPU使用记录窗口的句柄
wstring ClassNameToEnum;

BOOL CALLBACK EnumChildWindowsProc(HWND hWnd, LPARAM lParam) // 寻找CPU使用记录界面的子窗口ID
{
    wchar_t WndClassName[256];
    GetClassName(hWnd, WndClassName, 256);
    if (WndClassName == ClassNameToEnum && (EnumHWnd == 0 || [&hWnd]() {
        RECT cRect, tRect;
        GetWindowRect(hWnd, &tRect);
        GetWindowRect(EnumHWnd, &cRect);
        int tW = (tRect.right - tRect.left),
            tH = (tRect.bottom - tRect.top),
            cW = (cRect.right - cRect.left),
            cH = (cRect.bottom - cRect.top);
        return cW * cH < tW * tH;
    }())) {
        EnumHWnd = hWnd;
    }
    return true;
}

void FindWnd() 
{
    wcout << L"Try find " << config::WindowTitle << L" " << config::WindowClassName << endl;
    HWND TaskmgrHwnd = FindWindow(config::WindowClassName.c_str(), config::WindowTitle.c_str());
    if (TaskmgrHwnd != NULL) {
        if (config::ChildClassName.empty())
            EnumHWnd = TaskmgrHwnd;
        else {
            ClassNameToEnum = config::ChildClassName;
            EnumChildWindows(TaskmgrHwnd, EnumChildWindowsProc, NULL);
        }
    }
}

void Binarylize(Mat& src) {
    Mat bin, edge;
    cvtColor(src, bin, COLOR_BGR2GRAY);
    inRange(bin, Scalar(128, 128, 128), Scalar(255, 255, 255), bin);

    Canny(bin, edge, 80, 130);
    int gridHeight = src.cols / 10.0;
    int gridWidth = src.rows / 8.0;
    int gridOffset = clock() / 1000 * 10;
    for (int r = 0; r < src.rows; ++r) {
        for (int c = 0; c < src.cols; ++c) {
            src.at<Vec3b>(r, c) = ((bin.at<uchar>(r, c) == 255) ? config::colorBright : config::colorDark);
            if (config::DrawGrid)
                if (r % gridHeight == 0 || (c + gridOffset) % gridWidth == 0) src.at<Vec3b>(r, c) = config::colorGrid;
            if (edge.at<uchar>(r, c) == 255) src.at<Vec3b>(r, c) = config::colorEdge;
        }
    }
    rectangle(src, Rect{ 0, 0, src.cols, src.rows }, config::colorFrame, 0.1);
}

void CaptureScreen(Mat& screenFrame) {
    HDC hdcScreen = GetDC(NULL);
    HDC hdcMemDC = CreateCompatibleDC(hdcScreen);
    int width = GetSystemMetrics(SM_CXSCREEN);
    int height = GetSystemMetrics(SM_CYSCREEN);
    HBITMAP hbmScreen = CreateCompatibleBitmap(hdcScreen, width, height);
    SelectObject(hdcMemDC, hbmScreen);

    // 获取屏幕内容
    BitBlt(hdcMemDC, 0, 0, width, height, hdcScreen, 0, 0, SRCCOPY);
    
    // 将内存DC的内容读取到Mat中
    screenFrame = Mat(height, width, CV_8UC4);
    GetBitmapBits(hbmScreen, width * height * 4, screenFrame.data);

    DeleteObject(hbmScreen);
    DeleteDC(hdcMemDC);
    ReleaseDC(NULL, hdcScreen);
}

void Play() {
    cout << endl << endl << "--------------------------------------" << endl;
    config::ReadConfig();
    FindWnd(); // 寻找CPU使用记录的窗口, 函数会把窗口句柄保存在全局变量EnumHWnd
    if (EnumHWnd == NULL) {
        cout << "Can't find the window." << endl;
        return;
    }

    string wndName = "Screen Capture Player";
    namedWindow(wndName, WINDOW_NORMAL);
    HWND playerWnd = FindWindowA("Main HighGUI class", wndName.c_str());
    SetWindowLong(playerWnd, GWL_STYLE, GetWindowLong(playerWnd, GWL_STYLE) & (~(WS_BORDER | WS_CAPTION | WS_SYSMENU | WS_SIZEBOX)));
    SetWindowLong(playerWnd, GWL_EXSTYLE, WS_EX_TRANSPARENT | WS_EX_LAYERED);
    SetParent(playerWnd, EnumHWnd);

    RECT rect;
    GetWindowRect(EnumHWnd, &rect);
    SetWindowPos(playerWnd, HWND_TOPMOST, 0, 0, rect.right - rect.left, rect.bottom - rect.top, SWP_SHOWWINDOW);
    InvalidateRect(EnumHWnd, &rect, true);
    UpdateWindow(EnumHWnd);

    clock_t startTime = clock();
    int frameCount = 0;
    while (true) {
        Mat screenFrame;
        CaptureScreen(screenFrame);

        // 获取目标窗口尺寸
        GetWindowRect(EnumHWnd, &rect);
        int w = rect.right - rect.left;
        int h = rect.bottom - rect.top;
        MoveWindow(playerWnd, 0, 0, w, h, true);
        
        // 调整捕捉到的图像尺寸
        resize(screenFrame, screenFrame, Size(w, h), 0, 0, INTER_NEAREST);

        // 对图像进行二值化处理
        Binarylize(screenFrame);
        
        // 显示处理后的屏幕图像
        imshow(wndName, screenFrame);
        frameCount++;

        // 控制帧率
        clock_t endTime = clock();
        double frameTime = 1000.0 / 30;  // 假设目标帧率是30fps
        double elapsedTime = double(endTime - startTime) / CLOCKS_PER_SEC;
        double timeToWait = frameCount * frameTime / 1000.0 - elapsedTime;

        if (timeToWait > 0) {
            waitKey(timeToWait);
        }
    }

    destroyWindow(wndName);
}

int main() {
    std::locale::global(std::locale("zh_CN.UTF-8"));
    Play();
    system("pause");
}
