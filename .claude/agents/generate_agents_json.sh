#!/bin/bash

# generate-agents-json.sh
# Generates agents.json from markdown agent files in the current directory

set -euo pipefail

OUTPUT_FILE="agents.json"

# Function to extract YAML frontmatter field
extract_field() {
    local file="$1"
    local field="$2"
    
    # Extract the value between --- blocks, then get specific field
    awk -v field="$field" '
        /^---$/ { in_front++; next }
        in_front == 1 && $0 ~ "^" field ":" {
            sub("^" field ": *", "")
            # Remove quotes if present (both single and double)
            gsub(/^["'\'']/, "")
            gsub(/["'\'']$/, "")
            print
            exit
        }
    ' "$file"
}

# Function to extract tools array from frontmatter
extract_tools() {
    local file="$1"

    awk '
        /^---$/ { in_front++; next }
        in_front == 1 && /^tools:/ {
            # Handle inline array format: tools: ["Tool1", "Tool2"]
            if ($0 ~ /\[.*\]/) {
                match($0, /\[.*\]/)
                print substr($0, RSTART, RLENGTH)
                exit
            }
            # Start of array list format
            getline
            while ($0 ~ /^[[:space:]]*-/) {
                sub(/^[[:space:]]*-[[:space:]]*/, "")
                gsub(/^["'\''"]|["'\''"]\$/, "")
                tools = tools (tools ? ", " : "") "\"" $0 "\""
                getline
            }
            print "[" tools "]"
            exit
        }
    ' "$file"
}

# Function to extract prompt (everything after frontmatter)
extract_prompt() {
    local file="$1"

    awk '
        /^---$/ {
            if (++count == 2) {
                in_content = 1
                next
            }
            next
        }
        in_content {
            # Escape backslashes first, then quotes
            gsub(/\\/, "\\\\")
            gsub(/"/, "\\\"")
            # Add actual space between lines instead of \n
            if (NR > start_line + 1) printf " "
            printf "%s", $0
            if (start_line == 0) start_line = NR
        }
    ' "$file"
}

# Function to generate JSON for a single agent
generate_agent_json() {
    local file="$1"
    local name description tools prompt model

    name=$(extract_field "$file" "name")
    description=$(extract_field "$file" "description")
    tools=$(extract_tools "$file")
    model=$(extract_field "$file" "model")
    prompt=$(extract_prompt "$file")

    # Use name to create JSON key (convert to lowercase, replace spaces with hyphens)
    # Remove any quotes that might be in the name field
    local json_key=$(echo "$name" | sed 's/"//g' | tr '[:upper:]' '[:lower:]' | tr ' ' '-')

    # Default model to sonnet if not specified
    if [[ -z "$model" ]]; then
        model="sonnet"
    fi

    # Build JSON object
    cat <<EOF
  "$json_key": {
    "description": "$description",
    "prompt": "$prompt",
    "tools": $tools,
    "model": "$model"
  }
EOF
}

# Main script
main() {
    echo "Generating agents.json from markdown files..."

    # Find all .md files in current directory
    shopt -s nullglob
    md_files=(*.md)

    if [[ ${#md_files[@]} -eq 0 ]]; then
        echo "Error: No markdown files found in current directory"
        exit 1
    fi

    echo "Found ${#md_files[@]} agent file(s):"
    printf '  - %s\n' "${md_files[@]}"

    # Start JSON output
    echo "{" > "$OUTPUT_FILE"

    # Process each markdown file
    local first=true
    for md_file in "${md_files[@]}"; do
        echo "Processing $md_file..."

        # Add comma separator between agents (but not before first one)
        if [[ "$first" == "false" ]]; then
            echo "," >> "$OUTPUT_FILE"
        fi
        first=false

        # Generate and append agent JSON
        generate_agent_json "$md_file" >> "$OUTPUT_FILE"
    done

    # Close JSON object
    echo "" >> "$OUTPUT_FILE"
    echo "}" >> "$OUTPUT_FILE"

    echo ""
    echo "✓ Successfully generated $OUTPUT_FILE"
    echo ""
    echo "You can now use it with:"
    echo "  claude --agents $OUTPUT_FILE"
}

main "$@"
