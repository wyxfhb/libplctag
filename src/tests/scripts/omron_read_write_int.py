import time
from aphyt import omron


if __name__ == '__main__':
    print("Omron Read/Write Integer Test")
    with omron.NSeries('127.0.0.1') as eip_conn:
        for i in range(2):
            print(f"Iteration {i}:")
            myTag_value = eip_conn.read_variable('myTag[0]')
            print(f"myTag_value: {myTag_value}")
            myTag_value += 1
            eip_conn.write_variable('myTag[0]', myTag_value)
            myTag_value_after = eip_conn.read_variable('myTag[0]')
            print(f"myTag_value after write: {myTag_value_after}")
            time.sleep(.5)
