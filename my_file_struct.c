#ifndef MY_FILE_STRUCT_C
#define MY_FILE_STRUCT_C
#include <stdlib.h>
#include <string.h>

typedef struct my_file_struct_t {
    char* file_name;
    char* file_content;
    size_t file_content_size;
    char* uuid;
} MyFileStruct;

/**
 * @brief Serialize a MyFileStruct into a character buffer
 *
 * @param file_struct {MyFileStruct*} The structure to serialize
 * @param size_out {size_t*} The size of the produced buffer (for sending)
 * @return {char*} The serialized version of the structure
 */
char* serialize_my_file_struct(const MyFileStruct* file_struct, size_t* size_out) {
    size_t file_name_len = strlen(file_struct->file_name);
    size_t uuid_len = strlen(file_struct->uuid);

    size_t size = sizeof(size_t) + file_name_len +
        sizeof(size_t) + uuid_len +
        sizeof(size_t) + file_struct->file_content_size;

    char* buffer = malloc(size);
    if (!buffer) return NULL;

    char* p = buffer;

    // Serialize file_name
    memcpy(p, &file_name_len, sizeof(size_t));
    p += sizeof(size_t);
    memcpy(p, file_struct->file_name, file_name_len);
    p += file_name_len;

    // Serialize uuid
    memcpy(p, &uuid_len, sizeof(size_t));
    p += sizeof(size_t);
    memcpy(p, file_struct->uuid, uuid_len);
    p += uuid_len;

    // Serialize file_content_size
    memcpy(p, &file_struct->file_content_size, sizeof(size_t));
    p += sizeof(size_t);

    // Serialize file_content
    memcpy(p, file_struct->file_content, file_struct->file_content_size);
    p += file_struct->file_content_size;

    *size_out = size;
    return buffer;
}

/**
 * @brief Deserialize a MyFileStruct from a character buffer
 *
 * @param buffer {char*} The buffer to deserialize (should contain a MyFileStruct)
 * @return {MyFileStruct*} The deserialized structure
 */
MyFileStruct* deserialize_my_file_struct(const char* buffer) {
    const char* p = buffer;
    MyFileStruct* file_struct = malloc(sizeof(MyFileStruct));
    if (!file_struct) return NULL;

    size_t file_name_len;
    size_t uuid_len;

    // Deserialize file_name
    memcpy(&file_name_len, p, sizeof(size_t));
    p += sizeof(size_t);
    file_struct->file_name = malloc(file_name_len + 1);
    memcpy(file_struct->file_name, p, file_name_len);
    file_struct->file_name[file_name_len] = '\0';
    p += file_name_len;

    // Deserialize uuid
    memcpy(&uuid_len, p, sizeof(size_t));
    p += sizeof(size_t);
    file_struct->uuid = malloc(uuid_len + 1);
    memcpy(file_struct->uuid, p, uuid_len);
    file_struct->uuid[uuid_len] = '\0';
    p += uuid_len;

    // Deserialize file_content_size
    memcpy(&file_struct->file_content_size, p, sizeof(size_t));
    p += sizeof(size_t);

    // Deserialize file_content
    file_struct->file_content = malloc(file_struct->file_content_size);
    memcpy(file_struct->file_content, p, file_struct->file_content_size);
    p += file_struct->file_content_size;

    return file_struct;
}
#endif // MY_FILE_STRUCT_C