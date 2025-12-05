import argparse
import sys
import os

# Add parent directory to path to allow importing slcanx without installation
sys.path.append(os.path.abspath(os.path.join(os.path.dirname(__file__), '..', 'src')))

def get_parser(description):
    parser = argparse.ArgumentParser(description=description)
    parser.add_argument('--port', '-p', default='COM3', help='Serial port (default: COM3)')
    parser.add_argument('--bitrate', '-b', type=int, default=500000, help='Bitrate (default: 500000)')
    return parser
