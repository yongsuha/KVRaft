#ifndef M_SKIPLIST_H
#define M_SKIPLIST_H

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <mutex>
#include <vector>

#define STORE_FILE "store/dumpFile"
static std::string sep = ":";

template <typename K, typename V>
class Node
{
public:
    Node() {}
    Node(K key, V value, int level);
    K key() const;
    V value() const;
    void set_value(V value);
    ~Node();
public:
    Node<K, V>** _forward;
    int _node_level;
private:
    K _key;
    V _value;
};

template <typename K, typename V>
class SkipListDump
{
public:
    friend class boost::serialization::access;

    template <class Archive>
    void serialize(Archive& ar, const unsigned int version)
    {
        ar& _keyDumpVt;
        ar& _valueDumpVt;
    }
    std::vector<K> _keyDumpVt;
    std::vector<V> _valueDumpVt;

    void insert(const Node<K, V>& node);
};

template <typename K, typename V>
class SkipList
{
public:
    SkipList(int max_level);
    ~SkipList();
    int get_random_level();
    Node<K, V>* create_node(K key, V value, int level);
    bool insert_element(K key, V value);
    void display_list();
    bool search_element(K key, V& value);
    void delete_element(K key);
    void insert_set_element(K& key, V& value);
    std::string dump_file();
    void load_file(const std::string& dumpStr);
    void clear(Node<K, V>* node);
    int size();
private:
    void get_key_value_from_string(const std::string& str, std::string* key, std::string* value);
    bool is_valid_string(const std::string& str);
private:
    int _max_level;
    int _cur_level;
    Node<K, V>* _header;
    std::ofstream _file_writer;
    std::ifstream _file_reader;
    int _node_count;
    std::mutex _mutex;
};

#endif