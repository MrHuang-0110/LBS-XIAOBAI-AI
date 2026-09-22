#include "test.h"
#include "host_stub.h"

int test_proto_ble(void);
int test_program(void);
int test_remote(void);
int test_uartble_rx(void);
int test_display(void);

int g_test_pass = 0;
int g_test_fail = 0;

int main(void)
{
    test_proto_ble();
    test_program();
    test_remote();
    test_uartble_rx();
    test_display();

    printf("\n%d checks, %d failures\n", g_test_pass, g_test_fail);
    return (g_test_fail == 0) ? 0 : 1;
}
