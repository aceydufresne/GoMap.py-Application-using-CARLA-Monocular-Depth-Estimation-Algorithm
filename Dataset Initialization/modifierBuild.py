import os
import traceback
from core import G
import random as rand
import re
import math
from collections import defaultdict

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
    
    finalSet = {}
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
    
    genderModifier = human.getModifier("macrodetails/Gender")
    genderValue = 1.0 if sex else 0.0
    genderModifier.setValue(genderValue)
    human.setAgeYears(age)
    
    if age <= 10:
        #young children have a height ratio of around 6 heads
        heightRan = rand.uniform(5,6)
        heightVal = rand.uniform(45,54)
        headHeight = heightVal/ heightRan
                
    elif age > 10 and age < 14:
        heightRan = rand.uniform(6,7)
        #male = true
        if sex == True:
        #in inches with bias to average height
            heightVal = rand.triangular(59, 70, 63)
            #female = false
        else:
            heightVal = rand.triangular(51,67,63)
        headHeight = heightVal/ heightRan
        
    else:
        if sex == True:
            heightRan = rand.uniform(7,8)
            heightVal = rand.triangular(60, 84, 69)
        else:
            heightRan = rand.uniform(7,8)
            heightVal = rand.triangular(57, 76, 64)
        headHeight = heightVal / heightRan
    
    #determine height in MakeHumanAPI
    heightMod = human.getModifier("macrodetails-height/Height")
    maximumHeight = 96
    minimumHeight = 48
    normalizedLegHeight = (heightVal - minimumHeight) / (maximumHeight - minimumHeight)
    heightMod.setValue(normalizedLegHeight)
    
    #determining proportions of upper leg
    totalLegLength = 4 * headHeight
    upperLegVal = rand.uniform(.46,.48)
    lowerLegVal = 1.0-upperLegVal
    upperLeg = upperLegVal * totalLegLength
    lowerLeg = lowerLegVal * totalLegLength
    
    if not math.isclose(upperLegVal + lowerLegVal, 1.0):
        print("Error")
    else:
        upperLegMod = human.getModifier("armslegs/upperlegs-height-decr|incr")
    
        defaultValLeg = .47
        minimumHeightUpperLeg = .40
        maximumHeightUpperLeg = .54
        if upperLegVal < defaultValLeg:
            normalizedLegMod = (upperLegVal - defaultValLeg) / (defaultValLeg - minimumHeightUpperLeg)
        else:
            normalizedLegMod = (upperLegVal - defaultValLeg) / (maximumHeightUpperLeg - defaultValLeg)
        upperLegMod.setValue(normalizedLegMod)
        
    #lower legs:
        lowerLegMod = human.getModifier("armslegs/r-lowerarm-scale-vert-decr|incr")
        defaultValLowerLeg = .53
        minimumHeightLowerLeg = .46
        maximumHeightLowerLeg = .6
        
        if lowerLegVal < defaultValLowerLeg:
            normalizedLegModLower = ((lowerLegVal - defaultValLowerLeg) / (defaultValLowerLeg - minimumHeightLowerLeg))
        else:
            normalizedLegModLower = ((lowerLegVal - defaultValLowerLeg) / (maximumHeightLowerLeg - defaultValLowerLeg))
            #no modifier for lower leg
        #lowerLegMod.setValue(normalizedLegModLower)
        
    #arms:
    #total arm length in head units
    minimumHeightArm = 2.5
    maximumHeightArm = 3.6
    totalArmLengthVal = rand.uniform(2.5,3.6)
    #lower arm:
    lowerArmVal = rand.uniform(.45, .48)
    lowerArm = totalArmLengthVal * lowerArmVal
    #upper arm:
    upperArmVal = 1.0 - lowerArmVal
    upperArm = totalArmLengthVal * upperArmVal
    
    if not math.isclose(upperArmVal + lowerArmVal, 1.0):
        print("Error in arms")
    else:
        #lower arm value setting
        lowerArmMod = human.getModifier("armslegs/l-lowerarm-scale-vert-decr|incr")
        defaultValArmLower = .44
        minimumHeightLowerArm = .42
        maximumHeightLowerArm = .46
        if lowerArmVal < defaultValArmLower:
            normalizedArmModLower = (lowerArmVal - defaultValArmLower) / (defaultValArmLower - minimumHeightLowerArm)
        else:
            normalizedArmModLower = (lowerArmVal - defaultValArmLower) / (maximumHeightLowerArm - defaultValArmLower)
        lowerArmMod.setValue(normalizedArmModLower)
        #upper arm value setting
        upperArmMod = human.getModifier("armslegs/l-upperarm-scale-vert-decr|incr")
        defaultValArmUpper = .56
        minimumHeightUpperArm = .54
        maximumHeightUpperArm = .58
        if upperArmVal < defaultValArmUpper:
            normalizedArmModUpper = ((upperArmVal - defaultValArmUpper) / (defaultValArmUpper - minimumHeightUpperArm))
        else:
            normalizedArmModUpper = ((upperArmVal - defaultValArmUpper) / (maximumHeightUpperArm - defaultValArmUpper))
        upperArmMod.setValue(normalizedArmModUpper)
    
    pairs = []
    orientMod = {}
    nonOrientMod = []
    specialCase = {
        "macrodetails/Gender",
        "macrodetails/Age",
        "macrodetails-height/Height",
        "armslegs/upperlegs-height-decr|incr",
        "armslegs/lowerlegs-height-decr|incr",
        "armslegs/l-lowerarm-scale-vert-decr|incr",
        "armslegs/r-lowerarm-scale-vert-decr|incr",
        "armslegs/l-upperarm-scale-vert-decr|incr",
        "armslegs/r-upperarm-scale-vert-decr|incr",
}
    
    
    
    for modifier in modifiers:
        
        if modifier in specialCase:
            continue
        
        #check if the modifier uses the range -1,1 or 0,1
        #if "|" in modifier:
        #    pairs.append(modifier)
        #else:
        #    specialCase.append(modifier)
        
        #dictionary for
        sections = modifier.split(r'/')
                ##armslegs/l-leg-valgus-decr|incr
        #"armslegs" "l-leg-valgus-decr|incr"
        #sections[1] == l-leg-valgus-decr|incr"
        segments = sections[1].split("-", maxsplit=1)
        ##"armslegs" "l" "leg-valgus-decr|incr"
        modName = segments[1]
        tempSeg = sections[1]
        #"l"
        orientLetter= tempSeg[:1]
        sanityLetter = tempSeg[1]
        

        if orientLetter == "l" and sanityLetter == "-":
            if  modName in orientMod:
                orientMod[modName] += (modifier,)
            else:
                orientMod[modName] = (modifier,)
        elif orientLetter == "r" and sanityLetter == "-":
            if modName in orientMod:
                orientMod[modName] += (modifier,)
            else:
                orientMod[modName] = (modifier,)
        else:
            nonOrientMod.append(modifier,)
    
    specialMax = 1.0
    specialMin = 0.0
    maxVal = 1
    avgVal = 0
    minVal = -1
    finalSet = {}
    for key,val in orientMod.items():
        
        try:
            mod1,mod2 = val
        except ValueError:
            print(key)
        
        tempMod1 = human.getModifier(mod1)
        tempMod2 = human.getModifier(mod2)
        tempMin = max(tempMod1.getMin(), tempMod2.getMin())
        tempMax = min(tempMod1.getMax(), tempMod2.getMax())
        
        if mod1 in specialCase or mod2 in specialCase:
            tempRan = rand.uniform(tempMin, tempMax)
        
        else:
        
            tempRan = rand.triangular(tempMin, tempMax, 0.0)
        finalSet[mod1] = tempRan
        finalSet[mod2] = tempRan
        if finalSet[mod1] != finalSet[mod2]:
            print("error in modifier assignment")
            continue
        
    
    for key,val in finalSet.items():
        tempMod = human.getModifier(key)
        tempMod.setValue(val)
    for mod in nonOrientMod:
        tempMod = human.getModifier(mod)
        tempMin = tempMod.getMin()
        tempMax = tempMod.getMax()

        if tempMin >= 0.0:
            continue

        mode = max(tempMin, min(0.0, tempMax))
        modVal = rand.triangular(tempMin, tempMax, mode)
        tempMod.setValue(modVal)
    
    #muscle and fat assignment:
    
    
    
    
    
def test():
    testPath = "C:/Users/Acey/Documents/makehuman/v1py3/plugins/Dataset/test.txt"

    with open(testPath, "w") as output:
        output.write("createHuman loaded successfully")

    path = "C:/Users/Acey/Documents/makehuman/v1py3/plugins/Dataset/example1.fbx"
    
