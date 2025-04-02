import random

def generate_lines_to_file(filename, num_lines, prob_one):
    with open(filename, 'w') as f:
        for _ in range(num_lines):
            line = ''.join('1' if random.random() < prob_one else '0' for _ in range(9000))
            f.write(line + '\n')

# Example usage:
# Generate 10 lines with probability of 1 being 0.4, and save to 'output.txt'
generate_lines_to_file('output.txt', num_lines=300, prob_one=0.4)
