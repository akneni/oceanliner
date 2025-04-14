import random
import sys
import struct
import hashlib
import os

KEY_LEN = (1, 100)
VAL_LEN = (10, 1000)

def gen_txt_logfile(num_cmds: int) -> str:
    corpus = ''
    for _ in range(num_cmds):
        key = ''.join(chr(random.randint(97, 97+25)) for i in range(random.randint(*KEY_LEN)))
        value = ','.join(str(random.randint(0, 255)) for _ in range(random.randint(*VAL_LEN)))
        corpus += f"SET|{key}|{value}\n"
    return corpus.strip()

def gen_bin_logfile(num_cmds: int) -> bytes:
    # Generate random SET commands
    commands_data = bytearray()
    num_commands = 0
    
    for _ in range(num_cmds):
        # Generate random key and value
        key_len = random.randint(*KEY_LEN)
        key = ''.join(chr(random.randint(97, 97+25)) for _ in range(key_len))
        value_len = random.randint(*VAL_LEN)
        value = bytes([random.randint(0, 255) for _ in range(value_len)])
        
        # Command code (2 = SET)
        command_code = 2
        commands_data.extend(struct.pack("<B", command_code))
        
        # Key (null-terminated string)
        commands_data.extend(key.encode('utf-8'))
        commands_data.extend(b'\x00')
        
        # Padding for 8-byte alignment of value_length field
        padding_size = (8 - (len(commands_data) % 8)) % 8
        commands_data.extend(b'\x00' * padding_size)
        
        # Value length (uint64_t)
        commands_data.extend(struct.pack("<Q", len(value)))
        
        # Value
        commands_data.extend(value)
        
        num_commands += 1
    
    # Create magic number for header (16 bytes)
    magic_number = os.urandom(16)
    
    # Create LogEntryHeader
    term = 1
    log_index = 1
    command_length = len(commands_data)
    
    # Pack header without hash first
    header_without_hash = struct.pack("<IIQQxxxxxxxxxxxxxxxxxxxx", 
                                     term, 
                                     num_commands, 
                                     log_index, 
                                     command_length)
    header_without_hash += magic_number
    
    # Calculate header hash (24 bytes)
    header_hash = hashlib.sha256(header_without_hash).digest()[:24]
    
    # Now create the complete header
    header = struct.pack("<IIQQ", 
                        term, 
                        num_commands, 
                        log_index, 
                        command_length)
    header += header_hash
    header += magic_number
    
    # Ensure header is exactly 64 bytes
    assert len(header) == 64, f"Header size is {len(header)}, expected 64"
    
    # Assemble the final binary log file
    log_data = header + commands_data + header
    
    return log_data


def bin_to_txt_logfile(binary_file: str, output_file: str = None):
    """
    Convert a binary log file to text format.
    
    The binary format follows the Ocean Liner spec with:
    - LogEntryHeader (64 bytes)
    - Commands data
    - LogEntryHeader (64 bytes)
    
    The text format is:
    SET|key|value
    where value is comma-separated bytes
    """
    # Read binary data
    with open(binary_file, 'rb') as f:
        data = f.read()
    
    # Ensure we have at least two headers
    if len(data) < 128:
        raise ValueError("Binary file too small, missing headers")
    
    # Parse first LogEntryHeader
    header1 = data[:64]
    term1, num_commands1, log_index1, command_length1 = struct.unpack("<IIQQ", header1[:24])
    
    # Parse second LogEntryHeader (for validation)
    header2 = data[-64:]
    term2, num_commands2, log_index2, command_length2 = struct.unpack("<IIQQ", header2[:24])
    
    # Validate headers match
    if (term1 != term2 or num_commands1 != num_commands2 or 
        log_index1 != log_index2 or command_length1 != command_length2):
        print("Warning: Headers don't match, file may be corrupted")
    
    # Skip to commands section
    commands_data = data[64:64+command_length1]
    
    # Convert commands to text format
    text_lines = []
    offset = 0
    
    try:
        for i in range(num_commands1):
            # Read command code
            if offset >= len(commands_data):
                raise ValueError(f"Unexpected end of data at command {i+1}/{num_commands1}")
            
            command_code = commands_data[offset]
            offset += 1
            
            # Get command name
            command_name = {0: "NOP", 1: "GET", 2: "SET", 3: "DELETE"}.get(command_code, f"UNKNOWN_{command_code}")
            
            # Read null-terminated key
            key_start = offset
            while offset < len(commands_data) and commands_data[offset] != 0:
                offset += 1
            
            if offset >= len(commands_data):
                raise ValueError(f"Key not null-terminated in command {i+1}")
            
            key = commands_data[key_start:offset].decode('utf-8')
            offset += 1  # Skip null terminator
            
            # Skip padding for 8-byte alignment
            padding = (8 - (offset % 8)) % 8
            offset += padding
            
            # Read value_length
            if offset + 8 > len(commands_data):
                raise ValueError(f"Unexpected end of data at value_length in command {i+1}")
            
            value_length = struct.unpack("<Q", commands_data[offset:offset+8])[0]
            offset += 8
            
            # Read value
            if offset + value_length > len(commands_data):
                raise ValueError(f"Unexpected end of data at value in command {i+1}")
            
            value = commands_data[offset:offset+value_length]
            offset += value_length
            
            # Format value as comma-separated list of integers
            value_str = ','.join(str(b) for b in value)
            
            # Create text line
            text_line = f"{command_name}|{key}|{value_str}"
            text_lines.append(text_line)
    
    except Exception as e:
        print(f"Error parsing command data: {e}")
        # Continue with what we have so far
    
    # Combine lines into final text
    text_output = '\n'.join(text_lines)
    
    # Write to output file if specified
    if output_file:
        with open(output_file, 'w') as f:
            f.write(text_output)
    
    return text_output

if __name__ == "__main__":
    if len(sys.argv) <= 1:
        print("Expect argument for the number of commands to generate")
        sys.exit(1)
        
    

    if '--format=bin' in sys.argv:
        num_cmds = int([i for i in sys.argv if i.startswith('--num-rows=')][0].partition('=')[-1])
        blob = gen_bin_logfile(100)
        with open('assets/log-file-example-rand.bin', 'wb') as f:
            f.write(blob)
        exit()


    if '--format=txt' in sys.argv:
        num_cmds = int([i for i in sys.argv if i.startswith('--num-rows=')][0].partition('=')[-1])
        corpus = gen_txt_logfile(num_cmds)
        with open('assets/log-file-example-rand.txt', 'w') as f:
            f.write(corpus)

    if any(i.startswith('--to-text=') for i in sys.argv):
        filename = [i for i in sys.argv if i.startswith('--to-text=')][0].partition('=')[-1]

        outfile = [i for i in sys.argv if i.startswith('--out=')]
        if len(outfile) == 0:
            outfile = filename.rpartition('.')[0] + '.txt'
        else:
            outfile = [i for i in sys.argv if i.startswith('--out=')][0].partition('=')[-1]

        bin_to_txt_logfile(filename, outfile)
        

    
