#!/usr/bin/env python3
import argparse
import subprocess
import os
import shutil
from pathlib import Path


def encode_wav(wav_path, opus_bitrate='96', force=False):
    wav_path = Path(wav_path)
    base_name = wav_path.with_suffix('')
    
    try:
        # Get channels using sox instead of ffprobe
        sox_info = subprocess.run(
            ['sox', '--i', '-c', str(wav_path)],
            capture_output=True, text=True, check=True
        )
        channels = int(sox_info.stdout.strip())
    except Exception as e:
        print(f"Skipping {wav_path}: {str(e)}")
        return

    # Check if all opus files already exist
    all_exist = all(base_name.with_suffix(f'.opus{i}').exists() for i in range(channels))
    if all_exist and not force:
        print(f"Skipping {wav_path} - all opus tracks exist")
        return

    print(f"Processing {wav_path} ({channels} channels)...")

    # Create temp directory for channel splits
    temp_dir = Path(os.environ.get('TMP', '/tmp')) / f'opus_enc_{os.getpid()}'
    temp_dir.mkdir(exist_ok=True)

    try:
        for channel in range(channels):
            opus_path = base_name.with_suffix(f'.opus{channel}')
            
            # Use sox to read input and pipe raw PCM to opusenc
            sox_cmd = [
                'sox',
                str(wav_path),
                '-t', 'raw',
                '-e', 'signed-integer',  # Force specific PCM format
                '-b', '16',
                '-r', '48000',          # Resample to Opus preferred rate
                '-c', '1',              # Force mono output
                '-V1',                  # Enable verbose error reporting
                '-',                    # Output to stdout
                'remix', str(channel+1)  # Extract specific channel
            ]
            
            opusenc_cmd = [
                'opusenc',
                '--raw', '--raw-rate', '48000',
                '--bitrate', opus_bitrate,
                '--quiet',
                '-',                    # Read from stdin
                str(opus_path)
            ]

            # Pipe sox output directly to opusenc
            with subprocess.Popen(sox_cmd, stdout=subprocess.PIPE) as sox_proc:
                subprocess.run(
                    opusenc_cmd,
                    stdin=sox_proc.stdout,
                    check=True
                )

            print(f"Created {opus_path}")

    finally:
        # Cleanup temp files
        shutil.rmtree(temp_dir, ignore_errors=True)

def process_directory(root_dir, bitrate, force=False):
    for path in Path(root_dir).rglob('*.wav'):
        try:
            encode_wav(path, opus_bitrate=bitrate, force=force)
        except subprocess.CalledProcessError as e:
            print(f"Failed to process {path}: {str(e)}")
        except KeyboardInterrupt:
            print("\nAborted by user")
            return

if __name__ == '__main__':
    parser = argparse.ArgumentParser(
        description='Recursively encode WAV files to multi-track Opus',
        formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    parser.add_argument('directory', help='Root directory containing WAV files')
    parser.add_argument('-b', '--bitrate', default='96', 
                      help='Opus encoding bitrate per channel (kbps)')
    parser.add_argument('-f', '--force', action='store_true',
                      help='Re-encode existing Opus files')
    args = parser.parse_args()

    # Check for required tools
    required_tools = ['sox', 'opusenc']
    for tool in required_tools:
        if not shutil.which(tool):
            print(f"Error: Required tool '{tool}' not found in PATH")
            exit(1)

    process_directory(args.directory, args.bitrate, args.force)
    print("Done!")
