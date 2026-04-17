#pragma once

#include <string>
#include <vector>
#include <queue>
#include <ctime>

struct IcsEvent {
    std::string summary;
    std::string start_time; // Format: YYYYMMDDTHHMMSSZ
    std::string end_time;
    std::string location;

    void print() const;
    time_t get_start_time_t() const;

    // For Max Heap: we want the event with the LATEST time at the top
    // so we can pop it when a SOONER event comes in.
    bool operator<(const IcsEvent& other) const {
        return get_start_time_t() < other.get_start_time_t();
    }
};

class IcsParser {
public:
    IcsParser(size_t max_events = 10);
    ~IcsParser();

    // Parse a single line from the ICS file
    void parse_line(const std::string& line);

    // Get the list of parsed events (sorted by time)
    std::vector<IcsEvent> get_events() const;

    // Print all events to log
    void print_events() const;

    // Finalize parsing
    void flush();

    // Clear all events and reset state
    void clear();

    void set_max_events(size_t max) { max_events = max; }

private:
    size_t max_events;
    std::priority_queue<IcsEvent> event_heap;
    IcsEvent current_event;
    bool in_event;
    
    // For handling folded lines
    std::string unfolded_line;

    void process_unfolded_line(const std::string& line);
    void add_event_to_heap(const IcsEvent& event);
};
