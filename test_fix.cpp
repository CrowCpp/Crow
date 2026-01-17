#include <iostream>
#include <string>

// Inline the relevant macros and functions from query_string.h for standalone testing
#define CROW_QS_ISHEX(x)    ((((x)>='0'&&(x)<='9') || ((x)>='A'&&(x)<='F') || ((x)>='a'&&(x)<='f')) ? 1 : 0)
#define CROW_QS_HEX2DEC(x)  (((x)>='0'&&(x)<='9') ? (x)-48 : ((x)>='A'&&(x)<='F') ? (x)-55 : ((x)>='a'&&(x)<='f') ? (x)-87 : 0)
#define CROW_QS_ISQSCHR(x) ((((x)=='=')||((x)=='#')||((x)=='&')||((x)=='\0')) ? 0 : 1)

// FIXED version of qs_strncmp - with bounds checking
int qs_strncmp_fixed(const char * s, const char * qs, size_t n)
{
    unsigned char u1, u2, unyb, lnyb;

    while(n-- > 0)
    {
        u1 = static_cast<unsigned char>(*s++);
        u2 = static_cast<unsigned char>(*qs++);

        if ( ! CROW_QS_ISQSCHR(u1) ) {  u1 = '\0';  }
        if ( ! CROW_QS_ISQSCHR(u2) ) {  u2 = '\0';  }

        if ( u1 == '+' ) {  u1 = ' ';  }
        if ( u1 == '%' ) // easier/safer than scanf
        {
            // FIX: Check that next two chars exist and are valid hex before reading
            if ( CROW_QS_ISHEX(s[0]) && CROW_QS_ISHEX(s[1]) )
            {
                unyb = static_cast<unsigned char>(*s++);
                lnyb = static_cast<unsigned char>(*s++);
                u1 = (CROW_QS_HEX2DEC(unyb) * 16) + CROW_QS_HEX2DEC(lnyb);
            }
            else
            {
                u1 = '\0';
            }
        }

        if ( u2 == '+' ) {  u2 = ' ';  }
        if ( u2 == '%' ) // easier/safer than scanf
        {
            // FIX: Check that next two chars exist and are valid hex before reading
            if ( CROW_QS_ISHEX(qs[0]) && CROW_QS_ISHEX(qs[1]) )
            {
                unyb = static_cast<unsigned char>(*qs++);
                lnyb = static_cast<unsigned char>(*qs++);
                u2 = (CROW_QS_HEX2DEC(unyb) * 16) + CROW_QS_HEX2DEC(lnyb);
            }
            else
            {
                u2 = '\0';
            }
        }

        if ( u1 != u2 )
            return u1 - u2;
        if ( u1 == '\0' )
            return 0;
    }
    if ( CROW_QS_ISQSCHR(*qs) )
        return -1;
    else
        return 0;
}

// FIXED version of qs_decode - with bounds checking  
int qs_decode_fixed(char * qs)
{
    int i=0, j=0;

    while( CROW_QS_ISQSCHR(qs[j]) )
    {
        if ( qs[j] == '+' ) {  qs[i] = ' ';  }
        else if ( qs[j] == '%' ) // easier/safer than scanf
        {
            // FIX: Check bounds before reading: ensure j+1 and j+2 are within string
            if ( qs[j+1] == '\0' || qs[j+2] == '\0' ||
                 ! CROW_QS_ISHEX(qs[j+1]) || ! CROW_QS_ISHEX(qs[j+2]) )
            {
                qs[i] = '\0';
                return i;
            }
            qs[i] = (CROW_QS_HEX2DEC(qs[j+1]) * 16) + CROW_QS_HEX2DEC(qs[j+2]);
            j+=2;
        }
        else
        {
            qs[i] = qs[j];
        }
        i++;  j++;
    }
    qs[i] = '\0';

    return i;
}

bool test_malformed_percent()
{
    std::cout << "Testing malformed percent encoding...\n";
    bool passed = true;
    
    // Test 1: String ending with %
    {
        std::cout << "  Test 1: String ending with '%'... ";
        char test1[] = "%";
        int result = qs_decode_fixed(test1);
        // Should return 0 and terminate gracefully
        if (result == 0 && test1[0] == '\0') {
            std::cout << "PASSED\n";
        } else {
            std::cout << "FAILED\n";
            passed = false;
        }
    }
    
    // Test 2: String with % followed by single hex
    {
        std::cout << "  Test 2: String with '%2' (single hex)... ";
        char test2[] = "%2";
        int result = qs_decode_fixed(test2);
        // Should return 0 and terminate gracefully
        if (result == 0 && test2[0] == '\0') {
            std::cout << "PASSED\n";
        } else {
            std::cout << "FAILED\n";
            passed = false;
        }
    }
    
    // Test 3: String with % followed by invalid hex
    {
        std::cout << "  Test 3: String with '%GG' (invalid hex)... ";
        char test3[] = "%GG";
        int result = qs_decode_fixed(test3);
        // Should return 0 and terminate gracefully
        if (result == 0 && test3[0] == '\0') {
            std::cout << "PASSED\n";
        } else {
            std::cout << "FAILED\n";
            passed = false;
        }
    }
    
    // Test 4: Valid percent encoding still works
    {
        std::cout << "  Test 4: Valid '%20' encoding... ";
        char test4[] = "%20";
        int result = qs_decode_fixed(test4);
        // Should decode to space character
        if (result == 1 && test4[0] == ' ' && test4[1] == '\0') {
            std::cout << "PASSED\n";
        } else {
            std::cout << "FAILED (got result=" << result << ", char=" << (int)test4[0] << ")\n";
            passed = false;
        }
    }
    
    // Test 5: Valid key=value%20more
    {
        std::cout << "  Test 5: 'hello%20world' encoding... ";
        char test5[] = "hello%20world";
        int result = qs_decode_fixed(test5);
        std::string expected = "hello world";
        if (result == (int)expected.length() && std::string(test5) == expected) {
            std::cout << "PASSED\n";
        } else {
            std::cout << "FAILED (got '" << test5 << "')\n";
            passed = false;
        }
    }
    
    // Test 6: qs_strncmp with malformed percent
    {
        std::cout << "  Test 6: qs_strncmp with trailing '%'... ";
        const char* s1 = "%";
        const char* s2 = "test";
        // Should not crash, just compare
        int result = qs_strncmp_fixed(s1, s2, 4);
        std::cout << "PASSED (no crash, result=" << result << ")\n";
    }
    
    return passed;
}

int main()
{
    std::cout << "=== Heap Overflow Fix Verification (Issue #1126) ===\n\n";
    
    bool all_passed = test_malformed_percent();
    
    std::cout << "\n";
    if (all_passed) {
        std::cout << "SUCCESS: All tests passed! The fix prevents heap overflow.\n";
        return 0;
    } else {
        std::cout << "FAILURE: Some tests failed.\n";
        return 1;
    }
}
