import os
import traceback
from core import G
import random as rand
import re

def load(app):
    human = G.app.selectedHuman
    varList = "C:/Users/Acey/Documents/makehuman/v1py3/plugins/VariablesList.txt"
    modifiers = []
    #example, change on completion
    path = "C:/Users/Acey/Documents/makehuman/v1py3/plugins/Dataset/example1.fbx"
    
    #begin sorting the modifiers
    try:
        with open(varList, 'r') as input:
            for line in input:
                name = line.strip()
                
                #if the line is empty
                if not name:
                    continue
                else:
                    modifiers.append(name)
    except FileNotFoundError:
        print(f"'{varList}' is not working")
    except Exception as e:
        print(f"error: '{e}'")
    results = find(human,modifiers)

    #final export of model
    exporter = G.app.getExporter("fbx")
    exporter.export(human, path)


def unload(app):
    pass


def find(human,modifiers):
    
    #modifiers that must be set within a certain range of one another
    fatMods = ["armslegs/l-lowerarm-fat-decr|incr", "armslegs/l-lowerleg-fat-decr|incr", "armslegs/l-upperarm-fat-decr|incr", "armslegs/l-upperleg-fat-decr|incr", "armslegs/r-lowerarm-fat-decr|incr",
               "armslegs/r-lowerleg-fat-decr|incr", "armslegs/r-upperarm-fat-decr|incr", "armslegs/r-upperleg-fat-decr|incr","head/head-fat-decr|incr",
    ]
    muscleMods = ["armslegs/l-lowerarm-muscle-decr|incr", "armslegs/l-lowerleg-muscle-decr|incr", "armslegs/l-upperarm-muscle-decr|incr", "armslegs/l-upperarm-shoulder-muscle-decr|incr", "armslegs/l-upperleg-muscle-decr|incr",
               "armslegs/r-lowerarm-muscle-decr|incr", "armslegs/r-lowerleg-muscle-decr|incr", "armslegs/r-upperarm-muscle-decr|incr", "armslegs/r-upperarm-shoulder-muscle-decr|incr","armslegs/r-upperleg-muscle-decr|incr",
               "macrodetails-universal/Muscle", "torso/torso-muscle-dorsi-decr|incr", "torso/torso-muscle-pectoral-decr|incr",
               ]
    fat = {}
    muscle = {}
    #modifiers with no orientation, excluding fat/muscle/height
    noSymSet = {}
    symSet = {}
    #used to form ratios between modifiers, based on the davinci sketches
    headHeight = 0
    headHeightVal = rand.uniform(0.0,1.0)
    height = 0
    #modify this to be between a .008 and .001 chance
    dwarfism = rand.choice([True, False])
    age = rand.randint(3,70)
    height = 0
    sex = rand.choice([True, False])
    
    if age <= 10:
        #young children have a height ratio of around 6 heads
        heightRan = rand.uniform(5,6)
        heightVal = rand.uniform(45,54)
        headHeight = heightVal/ heightRan
                
    elif age > 10 and age < 14:
        height = rand.uniform(6,7)
        #male = true
        if sex == True:
        #in inches with bias to average height
            heightVal = rand.triangular(59, 66, 70)
            #female = false
        else:
            heightVal = rand.triangular(51,63,67)
        headHeight = heightVal/ heightRan
        
    else:
        if sex == True:
            height = rand.uniform(7,8)
            heigtVal = rand.triangular(60, 69, 84)
        else:
            height.rand.uniform(7,8)
            heightVal = rand.triangular(57, 64, 76)
        headHeight = heightVal / heightRan
    
    #determine height in MakeHumanAPI
    heightMod = human.getModifier("macrodetails-height/Height")
    maximumHeight = 96
    minimumHeight = 48
    normalizedHeight = (heightVal - minimumHeight) / (maximumHeight - minimumHeight)
    heightMod.setValue(normalizedHeight)
    
    #determining proportions of leg
    totalLegLength = 4 * headHeightVal
    upperLegVal = rand.uniform(.46,.48)
    lowerLegVal = 1-upperLegVal
    upperLeg = upperLegVal * totalLegLength
    lowerLeg = lowerLeg * totalLegLength
    
    if upperLegVal + lowerLegVal != 1:
        print("Error")
    else:
        upperLegMod = human.getModifier("armslegs/upperlegs-height-decr|incr")
        upperLegMod.setValue(upperLegVal)
    


def test():
    testPath = "C:/Users/Acey/Documents/makehuman/v1py3/plugins/Dataset/test.txt"

    with open(testPath, "w") as output:
        output.write("createHuman loaded successfully")

    path = "C:/Users/Acey/Documents/makehuman/v1py3/plugins/Dataset/example1.fbx"

