#include <opencv2/opencv.hpp>
using namespace cv;

class Kernel {
private:
    float kernelWeights[3][3];
public:
    Kernel() : kernelWeights{} {}

    void setter(float weights[3][3]) {
        //int rows = sizeof(kernelWeights) / sizeof(kernelWeights[0]);
        //int columns = sizeof(kernelWeights) / sizeof(kernelWeights[0][0]);

        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 3; j++) {
                kernelWeights[i][j] = weights[i][j];
            }
        }
    }

    void printKernel() {

        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 3; j++) {
                std::cout << kernelWeights[i][j] << " ";
            }
            std::cout << "\n";
        }
    }
};

void imageProcess(std::string filePath) {
    Mat image = imread(filePath, 0);
    imwrite(filePath, image);
    imshow("Grayscale", image);
    waitKey(0);
    
}

int main(void) {
    float w1 = 1, w2 = 0, w3 = -1, w4 = 2, w5 = 0, w6 = -2, w7 = 1, w8 = 0, w9 = -1;
    float weights[3][3] = {
        {w1,w2,w3},
        {w4,w5,w6},
        {w7,w8,w9}
    };
    Kernel kern;
    kern.setter(weights);
    kern.printKernel();
    std::string pathway = "patrick.png";
    imageProcess(pathway);

    return 0;
}
