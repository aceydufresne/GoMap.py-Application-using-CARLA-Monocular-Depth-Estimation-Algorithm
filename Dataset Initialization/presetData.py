import os
import re
import pandas as pd
import objaverse.xl as oxl


DOWNLOAD_DIR = r"E:\ObjaverseXL"
METADATA_DIR = os.path.join(DOWNLOAD_DIR, "metadata")

os.makedirs(DOWNLOAD_DIR, exist_ok=True)
os.makedirs(METADATA_DIR, exist_ok=True)

CATEGORY_TARGETS = {
    "characters_humanoids": 0.20,
    "animals_creatures": 0.15,
    "robots_mechs": 0.15,
    "vehicles": 0.10,
    "mechanical": 0.10,
    "sculptures_unusual": 0.10,
    "everyday_objects": 0.10,
    "random_diverse": 0.10,
}

TOTAL_TARGET = 50_000


CATEGORY_KEYWORDS = {

    "characters_humanoids": [
        "character",
        "human",
        "humanoid",
        "person",
        "man",
        "woman",
        "boy",
        "girl",
        "superhero",
        "anime",
        "cartoon",
        "warrior",
        "soldier",
        "knight",
    ],

    "animals_creatures": [
        "animal",
        "creature",
        "monster",
        "dragon",
        "dinosaur",
        "alien",
        "dog",
        "cat",
        "horse",
        "bird",
        "fish",
        "wolf",
        "bear",
        "insect",
        "spider",
        "snake",
    ],

    "robots_mechs": [
        "robot",
        "android",
        "mech",
        "mecha",
        "cyborg",
        "droid",
        "machine character",
        "robotic",
    ],

    "vehicles": [
        "car",
        "truck",
        "motorcycle",
        "bike",
        "vehicle",
        "aircraft",
        "airplane",
        "helicopter",
        "tank",
        "ship",
        "spaceship",
        "train",
    ],

    "mechanical": [
        "machine",
        "mechanical",
        "engine",
        "motor",
        "gear",
        "industrial",
        "tool",
        "mechanism",
        "equipment",
        "device",
    ],

    "sculptures_unusual": [
        "sculpture",
        "statue",
        "abstract",
        "fantasy",
        "artifact",
        "ornament",
        "surreal",
        "organic",
        "structure",
    ],

    "everyday_objects": [
        "chair",
        "table",
        "lamp",
        "bottle",
        "cup",
        "mug",
        "phone",
        "computer",
        "keyboard",
        "shoe",
        "bag",
        "furniture",
        "appliance",
    ],
}


print("Loading Objaverse-XL annotations...")

annotations = oxl.get_annotations(
    download_dir=DOWNLOAD_DIR
)


possible_text_columns = [
    "name",
    "title",
    "description",
    "tags",
    "category",
    "categories",
    "caption",
]

text_columns = [col for col in possible_text_columns if col in annotations.columns]


def build_search_text(row):
    parts = []

    for column in text_columns:
        value = row[column]

        if pd.notna(value):parts.append(str(value))

    return " ".join(parts).lower()


annotations["search_text"] = annotations.apply(build_search_text,axis=1)

def contains_keyword(text, keyword):
    pattern = r"\b" + re.escape(keyword.lower()) + r"\b"
    return re.search(pattern, text) is not None

def classify_object(text):
    scores = {}

    for category, keywords in CATEGORY_KEYWORDS.items():
        score = 0

        for keyword in keywords:
            if contains_keyword(text, keyword):
                score += 1

        scores[category] = score

    best_category = max(scores, key=scores.get)

    if scores[best_category] == 0:
        return "unclassified"

    return best_category


annotations["category_group"] = annotations["search_text"].apply(classify_object)

selected_groups = []
used_indices = set()

for category, fraction in CATEGORY_TARGETS.items():

    target_count = int(TOTAL_TARGET * fraction)

    if category == "random_diverse":

        remaining = annotations[~annotations.index.isin(used_indices)]
        count = min(target_count,len(remaining))
        sample = remaining.sample(n=count,random_state=42)

    else:
        candidates = annotations[annotations["category_group"]== category]

        count = min(target_count,len(candidates))

        if count == 0:
            print(f"No candidates found for "f"{category}")
            continue

        sample = candidates.sample(n=count,random_state=42)

    selected_groups.append(sample)
    used_indices.update(sample.index)


selected = pd.concat(selected_groups,ignore_index=False)
selected = selected.drop_duplicates()
selection_path = os.path.join(METADATA_DIR,"selected_objects.csv")

selected.to_csv(selection_path,index=False)


oxl.download_objects(objects=selected,download_dir=DOWNLOAD_DIR)
