import cv2
import os
from matplotlib import pyplot as plt

test_image = r'F:\CARLA set\renders\github\0bea666bb818844da58b5d5f87b1ed219f950bed79b1600384071c4ad0039ba4\000.png'

def edge_detection(imageFile):
    img = cv2.imread(imageFile)
    print(img)
    
    canny = cv2.Canny(img, threshold1 = 180, threshold2 = 200)
    plt.imshow(cv2.cvtColor(canny, cv2.COLOR_BGR2RGB))
    plt.title("Frame View")
    plt.show()

if __name__ == "__main__":
    edge_detection(test_image)
