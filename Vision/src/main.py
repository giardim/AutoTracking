####################################################
# Author: Michael Giardina
# Date: 1 March 2026
# Purpose: Finds the position of the target
#               (a blue hat) and sends the X and Y
#               coordinates to an ESP32 which will
#               move a motor to the direction of the 
#               target
#################################################### 

#ToDo:
# Map the camera space to real space
# Connect to ESP32
# Send the coordinates to the ESP32


import cv2 
import numpy as np

def main():
    cap = cv2.VideoCapture(0)

    #This is a 3 by 3 kernel which will be used for morphology
    kernel = np.ones([3, 3], dtype = np.uint8)

    while True:
        retStatus, frame = cap.read()

        if not retStatus:
            break

        #Flip the camera just to make it easier for the user (me) to follow
        frame = cv2.flip(frame, 1)

        #Convert to HSV, color detection is better with HSV
        hsv = cv2.cvtColor(frame, cv2.COLOR_BGR2HSV)

        #Mask out to only find the sharpie gray 
        lowerMask = np.array([100, 150, 60], dtype=np.uint8)
        upperMask = np.array([120, 255, 255], dtype=np.uint8)
        mask = cv2.inRange(hsv, lowerMask, upperMask)

        #Dialate then erode to fill the small holes in the target 
        #   (closing)
        mask = cv2.dilate(mask, kernel, iterations = 5)
        mask = cv2.erode(mask, kernel, iterations = 5)

        #find the centroid
        # Really good explaination of moments: https://stackoverflow.com/questions/22470902/understanding-moments-function-in-open 
        M = cv2.moments(mask)

        if M["m00"] != 0:
            centerX = int(M["m10"] / M["m00"])
            centerY = int(M["m01"] / M["m00"])
        else:
            centerX, centerY = None, None

        # draw a rectangle around the centroid
        if centerX is not None:
            box_size = 50

            startPoint = (centerX - box_size, centerY - box_size)
            endPoint   = (centerX + box_size, centerY + box_size)

            cv2.rectangle(frame, startPoint, endPoint, (0, 255, 0), 2)

        cv2.imshow("Original", frame)
        cv2.imshow("Mask", mask)

        #Escape key is to quit
        k = cv2.waitKey(5) & 0xFF
        if k == 27:
            break

    cv2.destroyAllWindows()
    cap.release()

main()