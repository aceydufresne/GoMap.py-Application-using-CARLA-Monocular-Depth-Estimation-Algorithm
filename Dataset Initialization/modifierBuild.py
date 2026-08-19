import os
import traceback
from core import G
import random as rand
import re
import math
from collections import defaultdict
import getpath
import skeleton as mh_skeleton

def load(app):
        
    human = G.app.selectedHuman
    varList = "C:/Users/Acey/Documents/makehuman/v1py3/plugins/VariablesList.txt"
    modifiers = []
    #example, change on completion
    path = "C:/Users/Acey/Documents/makehuman/v1py3/plugins/Dataset/example1.fbx"
    
    #first dataset inititalization
    OUTPUT_DIRECTORY = (
    "C:/Users/Acey/Documents/makehuman/"
    "v1py3/plugins/Dataset/unclothed"
)
    
    
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
    
    os.makedirs(OUTPUT_DIRECTORY, exist_ok=True)

    exportTask = app.getCategory("Files").getTaskByName("Export")
    exporter = exportTask.getExporter("Filmbox (fbx)")

    baselineValues = {
        name: human.getModifier(name).getValue()
        for name in modifiers
    }

    successfulExports = 0
    setSize = 100
    for index in range(1, setSize + 1):
        try:
            restoreModifierValues(human, baselineValues)
            find(human, modifiers)

            human.applyAllTargets()

            assignSkeleton(human)

            filename = "human_{:04d}".format(index)
            basePath = os.path.join(
                OUTPUT_DIRECTORY,
                filename
            )

            exporter.export(
                human,
                lambda extension, path=basePath:
                    path + "." + extension
            )

            successfulExports += 1
            print(
                "Exported {}/{}: {}.fbx".format(
                    index,
                    setSize,
                    basePath
                )
            )

        except Exception:
            print("Failed to generate human:", index)
            traceback.print_exc()

    print(
        "Finished: {}/{} exported".format(
            successfulExports,
            setSize
        )
    )


def unload(app):
    pass


def restoreModifierValues(human, baselineValues):
    human.blockEthnicUpdates = True

    try:
        for name, value in baselineValues.items():
            human.getModifier(name).setValue(value)
    finally:
        human.blockEthnicUpdates = False


def assignSkeleton(human, rigFilename="default.mhskel"):
    rigPath = getpath.getSysDataPath(
        os.path.join("rigs", rigFilename)
    )

    rig = mh_skeleton.load(rigPath, human.meshData)
    human.setSkeleton(rig)

    if human.getSkeleton() is None:
        raise RuntimeError(f"Failed to load skeleton: {rigPath}")

    print("Using skeleton:", rigFilename)
    


def find(human,modifiers):
    
    #rig:
    #enthinicity:

    alpha = 2.0

    rawAfrican = rand.gammavariate(alpha, 1.0)
    rawAsian = rand.gammavariate(alpha, 1.0)
    rawCaucasian = rand.gammavariate(alpha, 1.0)

    total = rawAfrican + rawAsian + rawCaucasian

    africanRatio = rawAfrican / total
    asianRatio = rawAsian / total
    caucasianRatio = rawCaucasian / total

    human.blockEthnicUpdates = True

    try:
        human.getModifier(
            "macrodetails/African"
        ).setValue(africanRatio)

        human.getModifier(
            "macrodetails/Asian"
        ).setValue(asianRatio)

        human.getModifier(
            "macrodetails/Caucasian"
        ).setValue(caucasianRatio)
    finally:
        human.blockEthnicUpdates = False
    
    
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
    
        #muscle and fat assignment:
# after age and sex have been selected

    if age <= 10:
        baseMuscle = rand.uniform(-0.8, -0.4)
        baseFat = rand.uniform(-0.2, 0.4)

    elif age < 14:
        if sex:
            baseMuscle = rand.uniform(-0.5, 0.1)
            baseFat = rand.uniform(-0.3, 0.3)
        else:
            baseMuscle = rand.uniform(-0.6, 0.0)
            baseFat = rand.uniform(-0.2, 0.4)

    elif age < 50:
        if sex:
            baseMuscle = rand.uniform(-0.2, 0.7)
            baseFat = rand.uniform(-0.4, 0.5)
        else:
            baseMuscle = rand.uniform(-0.3, 0.5)
            baseFat = rand.uniform(-0.2, 0.6)

    else:
        if sex:
            baseMuscle = rand.uniform(-0.5, 0.4)
            baseFat = rand.uniform(-0.2, 0.6)
        else:
            baseMuscle = rand.uniform(-0.6, 0.3)
            baseFat = rand.uniform(-0.1, 0.7)
            
    globalMuscleValue = (baseMuscle + 1.0) / 2.0

    globalMuscle = human.getModifier(
        "macrodetails-universal/Muscle"
    )

    globalMuscleValue = max(globalMuscle.getMin(),min(globalMuscle.getMax(), globalMuscleValue))

    globalMuscle.setValue(globalMuscleValue)
    
    muscleMap = {}
    nonOrientMuscle = []

    for modifier in muscleMods:
        if modifier == "macrodetails-universal/Muscle":
            continue

        sections = modifier.split("/")
        tempSeg = sections[1]
        orientLetter = tempSeg[0]
        sanityLetter = tempSeg[1]

        if orientLetter in ("l", "r") and sanityLetter == "-":
            segments = tempSeg.split("-", maxsplit=1)
            modName = segments[1]

            if modName in muscleMap:
                muscleMap[modName] += (modifier,)
            else:
                muscleMap[modName] = (modifier,)
        else:
            nonOrientMuscle.append(modifier)

    for key, value in muscleMap.items():

        if len(value) != 2:
            print("Missing muscle pair:", key)
            continue

        tempMod1, tempMod2 = value
        tempSet1 = human.getModifier(tempMod1)
        tempSet2 = human.getModifier(tempMod2)

        variation = rand.uniform(-0.15, 0.15)
        muscleValue = baseMuscle + variation

        minimum = max(tempSet1.getMin(), tempSet2.getMin())
        maximum = min(tempSet1.getMax(), tempSet2.getMax())

        muscleValue = max(minimum,min(maximum, muscleValue))
        tempSet1.setValue(muscleValue)
        tempSet2.setValue(muscleValue)

    for modifier in nonOrientMuscle:

        mod = human.getModifier(modifier)

        variation = rand.uniform(-0.15, 0.15)
        muscleValue = baseMuscle + variation

        muscleValue = max(mod.getMin(),min(mod.getMax(), muscleValue))
        mod.setValue(muscleValue)


    globalFatValue = (baseFat + 1.0) / 2.0

    globalFat = human.getModifier(
        "macrodetails-universal/Weight"
    )

    globalFatValue = max(globalFat.getMin(),min(globalFat.getMax(), globalFatValue))

    globalFat.setValue(globalFatValue)
    
    fatMap = {}
    nonOrientedFat = []
    
    for modifier in fatMods:
        if modifier == "macrodetails-universal/Weight":
            continue
        sections = modifier.split("/")
        tempSeg = sections[1]
        orientLetter = tempSeg[0]
        sanityLetter = tempSeg[1]

        if orientLetter in ("l", "r") and sanityLetter == "-":
            segments = tempSeg.split("-", maxsplit=1)
            modName = segments[1]

            if modName in fatMap:
                fatMap[modName] += (modifier,)
            else:
                fatMap[modName] = (modifier,)
        else:
            nonOrientedFat.append(modifier)
    
    for key, value in fatMap.items():
        if len(value) != 2:
            print("Missing muscle pair:", key)
            continue

        tempMod1, tempMod2 = value
        tempSet1 = human.getModifier(tempMod1)
        tempSet2 = human.getModifier(tempMod2)


        variation = rand.uniform(-0.15, 0.15)
        fatValue = baseFat + variation

        minimum = max(tempSet1.getMin(), tempSet2.getMin())
        maximum = min(tempSet1.getMax(), tempSet2.getMax())

        muscleValue = max(minimum,min(maximum, fatValue))
        tempSet1.setValue(fatValue)
        tempSet2.setValue(fatValue)

    for modifier in nonOrientedFat:

        mod = human.getModifier(modifier)
        variation = rand.uniform(-0.15, 0.15)
        fatValue = baseFat + variation
        fatValue = max(mod.getMin(),min(mod.getMax(), fatValue))
        mod.setValue(fatValue)


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
        lowerLegModR = human.getModifier("armslegs/r-lowerarm-scale-vert-decr|incr")
        lowerLegModL = human.getModifier("armslegs/r-lowerarm-scale-vert-decr|incr")
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
    totalArmLengthHeads = rand.uniform(2.7, 3.6)
    #height in inches
    lowerArmVal = rand.uniform(.42, .48)
    lowerArmLengthHeads = lowerArmVal * totalArmLengthHeads
    upperArmVal = 1 - lowerArmVal
    upperArmLengthHeads = upperArmVal * totalArmLengthHeads
    
    
    if not math.isclose(upperArmVal + lowerArmVal, 1.0):
        print("Error in arms")
    else:
        #lower arm value setting
        lowerArmModL = human.getModifier("armslegs/l-lowerarm-scale-horiz-decr|incr")
        lowerArmModR = human.getModifier("armslegs/r-lowerarm-scale-horiz-decr|incr")
        measureLowerArmMod = human.getModifier("measure/measure-lowerarm-length-decr|incr")
        defaultValArmLower = .45
        minimumHeightLowerArm = .42
        maximumHeightLowerArm = .48
        if lowerArmVal < defaultValArmLower:
            normalizedArmModLower = (lowerArmVal - defaultValArmLower) / (defaultValArmLower - minimumHeightLowerArm)
        else:
            normalizedArmModLower = (lowerArmVal - defaultValArmLower) / (maximumHeightLowerArm - defaultValArmLower)
            
            #both modifiers will work, but use the measure modifier
        #lowerArmModL.setValue(normalizedArmModLower)
        #lowerArmModR.setValue(normalizedArmModLower)
        measureLowerArmMod.setValue(normalizedArmModLower)
        
        #upper arm value setting
        upperArmModL = human.getModifier("armslegs/l-upperarm-scale-horiz-decr|incr")
        upperArmModR = human.getModifier("armslegs/r-upperarm-scale-horiz-decr|incr")
        measureArmMod = human.getModifier("measure/measure-upperarm-length-decr|incr")
        defaultValArmUpper = .55
        minimumHeightUpperArm = .52
        maximumHeightUpperArm = .58
        if upperArmVal < defaultValArmUpper:
            normalizedArmModUpper = ((upperArmVal - defaultValArmUpper) / (defaultValArmUpper - minimumHeightUpperArm))
        else:
            normalizedArmModUpper = ((upperArmVal - defaultValArmUpper) / (maximumHeightUpperArm - defaultValArmUpper))
            #both modifiers will work, but use the measure modifier
        #upperArmModL.setValue(normalizedArmModUpper)
        #upperArmModR.setValue(normalizedArmModUpper)
        measureArmMod.setValue(normalizedArmModUpper)
    
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
        "measure/measure-upperarm-length-decr|incr",
        "measure/measure-lowerarm-length-decr|incr",
        "macrodetails/Caucasian",
        "macrodetails/Asian",
        "macrodetails/African"
}
    specialCase.update(fatMods)
    specialCase.update(muscleMods)
    
    for modifier in modifiers:
        if modifier in specialCase:
            continue

        sections = modifier.split("/", maxsplit=1)

        if len(sections) != 2:
            print("Invalid modifier name:", modifier)
            continue

        tempSeg = sections[1]

        # Only left/right modifiers have the l- or r- prefix.
        if (
            len(tempSeg) >= 3
            and tempSeg[0] in ("l", "r")
            and tempSeg[1] == "-"
        ):
            modName = tempSeg[2:]

            if modName in orientMod:
                orientMod[modName] += (modifier,)
            else:
                orientMod[modName] = (modifier,)
        else:
            nonOrientMod.append(modifier)
    #for modifier in modifiers:
        
        #if modifier in specialCase:
         #   continue
        
        #check if the modifier uses the range -1,1 or 0,1
        #if "|" in modifier:
        #    pairs.append(modifier)
        #else:
        #    specialCase.append(modifier)
        
        #dictionary for
        #sections = modifier.split(r'/')
                ##armslegs/l-leg-valgus-decr|incr
        #"armslegs" "l-leg-valgus-decr|incr"
        #sections[1] == l-leg-valgus-decr|incr"
        #segments = sections[1].split("-", maxsplit=1)
        ##"armslegs" "l" "leg-valgus-decr|incr"
        #modName = segments[1]
        #tempSeg = sections[1]
        #"l"
        #orientLetter= tempSeg[:1]
        #sanityLetter = tempSeg[1]
        

        #if orientLetter == "l" and sanityLetter == "-":
            #if  modName in orientMod:
           #     orientMod[modName] += (modifier,)
          #  else:
         #       orientMod[modName] = (modifier,)
        #elif orientLetter == "r" and sanityLetter == "-":
           # if modName in orientMod:
             #   orientMod[modName] += (modifier,)
            #else:
                #orientMod[modName] = (modifier,)
        #else:
            #nonOrientMod.append(modifier,)
    
    specialMax = 1.0
    specialMin = 0.0
    maxVal = 1
    avgVal = 0
    minVal = -1
    finalSet = {}
    for key,val in orientMod.items():
        
        if len(val) != 2:
            print("Missing modifier pair:", key, val)
            continue

        mod1, mod2 = val
        
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
    

    
    
    
def test():
    testPath = "C:/Users/Acey/Documents/makehuman/v1py3/plugins/Dataset/test.txt"

    with open(testPath, "w") as output:
        output.write("createHuman loaded successfully")

    path = "C:/Users/Acey/Documents/makehuman/v1py3/plugins/Dataset/example1.fbx"
    
