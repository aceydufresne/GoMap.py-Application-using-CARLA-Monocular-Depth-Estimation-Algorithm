import json

globalTokens = []

def tokenizer(path):
    global globalTokens
    with open(path, "r", encoding="utf-8") as file:
        for line in file:
            conversation = line.strip()
            #end of utterance
            eou = conversation.split("__eou__")

            for utterance in eou:
                utterance = utterance.strip()

                if utterance:
                    
                    byteData = utterance.encode("utf-8")
                    tokens = list(byteData)
                    globalTokens.append(tokens)
    #now begin finding the patternsL
    
    merges = {}
    currentByte = 256
    
    while currentByte<2000:
        pairCounts = {}
        
        for unqToken in globalTokens:
            for i in range(len(unqToken)-1):
                pair = (unqToken[i], unqToken[i+1])
                pairCounts[pair] = pairCounts.get(pair,0) + 1
                
                #most common pattern
        if not pairCounts:
            break
        highestComm = max(pairCounts,key=pairCounts.get)
        updatedToken = currentByte
        merges[highestComm] = updatedToken
        updateGlobalTokens = []
        
        for unqToken in globalTokens:
            newTokens = []
            i = 0
        #replace the old pattern with a standard new byte
            while i < len(unqToken):
                if (i < len(unqToken) - 1 and (unqToken[i], unqToken[i + 1]) == highestComm):
                    newTokens.append(updatedToken)
                    i += 2
                else:
                    newTokens.append(unqToken[i])
                    i += 1
                
            updateGlobalTokens.append(newTokens)
        globalTokens = updateGlobalTokens
        currentByte += 1
    
    mergeData = []
    for pair, tokenID in merges.items():
        mergeData.append([pair[0],pair[1],tokenID])
    
    with open("tokenizer_merges.json", "w") as file:
        json.dump(mergeData, file)
