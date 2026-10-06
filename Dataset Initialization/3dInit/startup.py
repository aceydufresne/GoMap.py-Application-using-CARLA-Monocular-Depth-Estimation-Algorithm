import objaverse.xl as oxl
import pandas as pd
import os
import json
import time
import shutil
import requests
import hashlib
from urllib.parse import urlparse, parse_qs


def sketchFabSelector(descriptions):
    #limiting only to the set which has no description values
    sketchFab = descriptions[descriptions["source"]=="sketchfab"]
    csvPath = r"F:\CARLA set\meta_data\sketchfab_selected.csv"
    downloadPath = r"F:\CARLA set\Sketchfab"
    #keep track of what is in the folder already
    target = 10
    
    if os.path.exists(csvPath):
        #if we have already run the script before.
        previous = pd.read_csv(csvPath)
        #sha256 is all the byte data, 
        previousVals = set(previous["sha256"])
        sketchFab = sketchFab[~sketchFab["sha256"].isin(previousVals)]
    else:
        #we have not run the script before/first test
        previous = pd.DataFrame()
        #a uniform sample with no partitions
    sample = sketchFab.sample(n=min(target, len(sketchFab)), random_state = 42)   
    
    if not previous.empty:
        #if something is already populated in the folder, we add to it instead of creating new hash data
        allSelected = pd.concat([previous,sample], ignore_index = True)
        
    else:
        #the set becomes the initial random seed
        allSelected = sample
        #sanity check, remove all the matching cases of byte data
    allSelected = allSelected.drop_duplicates(subset = "sha256")
    allSelected.to_csv(csvPath, index = False)
    oxl.download_objects(objects=sample, download_dir=downloadPath)
    


def smithsonianSelector(descriptions):
    #very simple for this set, with only 2000 elements it is too small to partition
    csvPath = r"F:\CARLA set\meta_data\smithsonian_selected.csv"
    #sanity guard
    if os.path.exists(csvPath):
        return
    
    smithsonianSet = descriptions[descriptions["source"] == "smithsonian"].copy()
    path = r"F:\CARLA set\Smithsonian"
    oxl.download_objects(objects=smithsonianSet, download_dir=path)
    

def githubSelector(descriptions):
    path = r"F:\CARLA set\meta_data\github_selected.csv"
    downloadPath = r"F:\CARLA set\github\github"
    df = pd.read_csv(path)
    #print(df.head())
    #iterate through document:
    target = 10
    sample = df.sample(n=min(target,len(df)), random_state = 42)
    
    for row in sample[["fileIdentifier", "fileType", "sha256"]].itertuples(index = False):
        url = row.fileIdentifier
        fileType = row.fileType
        sha256 = row.sha256
        #convert to scrape the actual files, that git viewer
        rawURL = url.replace("https://github.com/", "https://raw.githubusercontent.com/").replace("/blob/", "/")
        #print(rawURL)
        
        #check if this is already in the set
        fileName = f"{sha256}.{fileType}"
        filePath = os.path.join(path, fileName)
        if os.path.exists(filePath):
            continue
        
        try:
            response = requests.get(rawURL, timeout= 60)
            response.raise_for_status()
            fileContents = response.content
            actualHash = hashlib.sha256(fileContents).hexdigest()
            
            if actualHash != sha256:
                print("Errir")
                continue
            
            filePath = os.path.join(downloadPath, fileName)
            with open(filePath, "wb") as file:
                file.write(fileContents)
        except requests.RequestException as e:
            print(rawURL)
            print(e)
            
            
def thingiverseSelector(descriptions):
    path = r"F:\CARLA set\meta_data\thingiverse_selected.csv"

    df = pd.read_csv(path)

    for row in df[
        ["fileIdentifier", "fileType", "sha256", "metadata"]
    ].itertuples(index=False):

        url = row.fileIdentifier

        query = parse_qs(urlparse(url).query)
        fileID = query["fileId"][0]

        thingID = url.split("thing:")[1].split("/")[0]

        metadata = json.loads(row.metadata)
        filename = metadata["filename"]

        print("Thing:", thingID)
        print("File:", fileID)
        print("Filename:", filename)
        print()
    
               
    
if __name__ == "__main__":
    directory = r"F:\CARLA set\meta_data"
#this is a file containing the descriptions and notes for categories of models
    descriptions = oxl.get_annotations(download_dir= directory)
    sources = descriptions["source"].unique()
    #sketchFabSelector(descriptions)
    #do not run smithsonian set more than once
    #smithsonianSelector(descriptions)
    #print(descriptions["source"].value_counts())
    path = r"F:\CARLA set\Smithsonian"
    #githubSelector(descriptions)
    #print("Exists:", os.path.exists(path))
    #print("Contents:", os.listdir(path))
    thingiverseSelector(descriptions)
    
