import sys
import pstats
from pstats import SortKey

def main():
    if len(sys.argv) != 2:
        print("Usage: python analyze_profile.py <profile_file.prof>")
        sys.exit(1)

    profile_file = sys.argv[1]

    try:
        p = pstats.Stats(profile_file)
        p.strip_dirs().sort_stats(SortKey.CUMULATIVE).print_stats(20)
    except FileNotFoundError:
        print(f"File not found: {profile_file}")
    except Exception as e:
        print(f"Error reading profile file: {e}")

if __name__ == "__main__":
    main()
