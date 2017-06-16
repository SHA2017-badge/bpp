void
hexdump(unsigned char *buf, int size)
{
    int pos; 
    for (pos = 0; pos < size; pos += 16)
    {
        printf("[%04x]  ", pos);
        int i;
        for (i=0; (i<16) && (pos+i < size); i++)
            printf("%02x ", buf[pos+i]);
        for (; i<16; i++)
            printf("   ");
        printf(" ");
        for (i=0; (i<16) && (pos+i < size); i++)
        {
            char ch = buf[pos+i];
            if (ch < ' ' || ch > 0x7e)
                ch = '.';
            printf("%c", ch);
        }
        printf("\n");
    }
}
