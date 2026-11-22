#include <cstdint>
#include <array>

namespace tecflix {

// Define constants for fixed sizes
constexpr size_t MAX_USERNAME_LENGTH = 32;
constexpr size_t MAX_EMAIL_LENGTH = 64;

// Fixed-size structure for User
struct Usuario {
    uint64_t id; // Unique identifier for the user
    char username[MAX_USERNAME_LENGTH]; // Username with fixed size
    char email[MAX_EMAIL_LENGTH]; // Email with fixed size
    uint8_t age; // Age of the user
    bool isActive; // Status of the user account

    // Constructor to initialize the user structure
    Usuario(uint64_t _id = 0, const char* _username = "", const char* _email = "", uint8_t _age = 0, bool _isActive = false)
        : id(_id), age(_age), isActive(_isActive) {
        // Initialize username and email with empty strings
        std::fill(std::begin(username), std::end(username), '\0');
        std::fill(std::begin(email), std::end(email), '\0');

        // Copy provided username and email into the fixed-size arrays
        if (_username) {
            std::copy_n(_username, std::min(strlen(_username), MAX_USERNAME_LENGTH - 1), username);
        }
        if (_email) {
            std::copy_n(_email, std::min(strlen(_email), MAX_EMAIL_LENGTH - 1), email);
        }
    }

    // Function to print user details (for debugging purposes)
    void print() const {
        printf("ID: %lu\n", id);
        printf("Username: %s\n", username);
        printf("Email: %s\n", email);
        printf("Age: %u\n", age);
        printf("Is Active: %s\n", isActive ? "true" : "false");
    }
};

} // namespace tecflix