import cv2 as cv
import os


def preRecordedInput(video, outputDir):
    frames = []

    os.makedirs(outputDir, exist_ok=True)

    while video.isOpened():
        flag, frame = video.read()

        if not flag:
            break

        framePath = os.path.join(
            outputDir, f"frame_{len(frames):06d}.png"
        )

        cv.imwrite(framePath, frame)
        frames.append(frame)

    video.release()
    return frames


def liveInput(video, outputDir):
    os.makedirs(outputDir, exist_ok=True)

    frameCount = 0

    while video.isOpened():
        flag, frame = video.read()

        if not flag:
            break

        framePath = os.path.join(
            outputDir, f"frame_{frameCount:06d}.png"
        )

        cv.imwrite(framePath, frame)

        cv.imshow("Live Video Feed", frame)

        frameCount += 1

        # Press Q to terminate capture
        if cv.waitKey(1) & 0xFF == ord("q"):
            break

    video.release()
    cv.destroyAllWindows()

    return frameCount



if __name__ == "__main__":
    inputMode = os.getenv("INPUT_MODE", "live")
    inputPath = os.getenv("INPUT_PATH")

    outputDir = r"F:\CARLA\model\output\algorithmOutput"

    if inputMode == "live":
        video = cv.VideoCapture(0)

    elif inputMode == "recorded":
        if not inputPath:
            raise ValueError("INPUT_PATH is not defined")

        video = cv.VideoCapture(inputPath)

    else:
        raise ValueError("Invalid input mode")

    if not video.isOpened():
        raise RuntimeError("Unable to open video source")

    if inputMode == "live":
        liveInput(video, outputDir)
    else:
        preRecordedInput(video, outputDir)

    print(outputDir)
