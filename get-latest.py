import argparse
from repo_utils import getLatestVersion

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("-n", "--name", required=True)
    args = parser.parse_args()

    result = getLatestVersion(args.name)
    if result:
        print(result)
