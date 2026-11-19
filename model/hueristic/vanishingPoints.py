import cv2
import numpy as np
import matplotlib.pyplot as plt
import os


def loadExample(path):
    img = cv2.imread(path)
    if img is None:
        print("error loading image")
        return
    else:
        gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
        blur = cv2.GaussianBlur(img, (5,5),3)
        cannyEdge = cv2.Canny(blur, 50 ,180)
        
        plt.figure()
        plt.subplot(141)
        plt.imshow(blur)
        plt.subplot(143)
        plt.imshow(cannyEdge)
        #plt.show()
        
        distResol = 1
        angleResol = np.pi/180
        threshold = 100
        lines = cv2.HoughLines(cannyEdge, distResol, angleResol, threshold)
        k = 3000
        for curLine in lines:
            rho, theta = curLine[0]
            dhat = np.array([[np.cos(theta)], [np.sin(theta)]])
            d = rho * dhat
            lhat = np.array([[-np.sin(theta)], [np.cos(theta)]])
            p1 = d+ k * lhat
            p2 = d - k * lhat
            p1 = p1.astype(int)
            p2 = p2.astype(int)
            cv2.line(img, (p1[0][0], p1[1][0]), (p2[0][0], p2[1][0]), (255,255,255), 10)
        plt.subplot(144)
        plt.imshow(img)
        plt.show()
        
if __name__ == "__main__":
    path = r'F:\CARLA\additional\BoothExample.jpg'
    loadExample(path)
