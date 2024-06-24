#include "./include/skipList.h"

template <typename K, typename V>
Node<K, V>::Node(K key, V value, int level)
    : _key(key), _value(value), _node_level(level),
    _forward(new Node<K, V>* [level + 1])
{
    memset(_forward, 0, sizeof(Node<K, V>*) * (level + 1));
}

template <typename K, typename V>
K Node<K, V>::key() const
{
    return _key;
}

template <typename K, typename V>
V Node<K, V>::value() const
{
    return _value;
}

template <typename K, typename V>
void Node<K, V>::set_value(V value)
{
    _value = value;
}

template <typename K, typename V>
Node<K, V>::~Node()
{
    delete[] _forward;
}

template <typename K, typename V>
SkipList<K, V>::SkipList(int max_level)
    : _max_level(max_level), _cur_level(0), _node_count(0),
    _header(new Node<K, V>(K(), V(), _max_level)) 
{}

template <typename K, typename V>
SkipList<K, V>::~SkipList()
{
    if (_file_reader.is_open())
    {
        _file_reader.close();
    }
    if (_file_writer.is_open())
    {
        _file_writer.close();
    }
    // 递归删除调表节点
    if (_header->_forward[0] != nullptr)
    {
        clear(_header->_forward[0]);
    }
    delete _header;
}

template <typename K, typename V>
int SkipList<K, V>::get_random_level()
{
    int k = 1;
    while (rand() % 2)
    {
        k++;
    }
    k = (k < _max_level) ? k : _max_level;
    return k;
}

template <typename K, typename V>
Node<K, V>* SkipList<K, V>::create_node(K key, V value, int level)
{
    Node<K, V>* node = new Node<K, V>(key, value, level);
    return node;
}

template <typename K, typename V>
bool SkipList<K, V>::insert_element(K key, V value)
{
    _mutex.lock();
    Node<K, V>* cur = _header;
    // 插入节点需要先找到插入的位置, 再根据新节点的层数进行连接
    // 先创建一个临时节点, 用于记录在找插入位置过程中, 新节点每一层合适的连接节点
    Node<K, V>* update[_max_level + 1];
    memset(update, 0, sizeof(Node<K, V>*) * (_max_level + 1));
    for (int i = _cur_level; i >= 0; i--)
    {
        while (cur->_forward[i] != nullptr && cur->_forward[i]->key() < key)
        {
            cur = cur->_forward[i];
        }
        update[i] = cur;
    }
    // 到第0层, 将其与右边节点相连
    cur = cur->_forward[0];
    // 如果在过程中找到了与希望插入节点值相同的节点, 则不用插入直接返回
    if (cur != nullptr && cur->key() == key)
    {
        std::cout << "key: " << key << ", already exists!!" << std::endl;
        _mutex.unlock();
        return false;
    }
    // 如果 cur 为空则 level 已经走完, 如果 cur 的 key 不相等则需要再两个节点之间插入
    if (cur == nullptr || cur->key() != key)
    {
        int random_level = get_random_level();
        // 如果得到的层数大于当前链表的层数, 则将节点与头节点相连
        if (random_level > _cur_level)
        {
            for (int i = _cur_level + 1; i < random_level + 1; i++)
            {
                update[i] = _header;
            }
            _cur_level = random_level;
        }
        // 创建新节点并插入
        Node<K, V>* insert_node = create_node(key, value, random_level);
        for (int i = 0; i <= random_level; i++)
        {
            insert_node->_forward[i] = update[i]->_forward[i];
            update[i]->_forward[i] = insert_node;
        }
        std::cout << "The key is inserted successfully, key: " << key << ", value: " << value << std::endl;
        _node_count++;
    }
    _mutex.unlock();
    return true;
}

template <typename K, typename V>
void SkipList<K, V>::display_list()
{
    std::cout << "\n******Skip List******" << std::endl;
    for (int i = 0; i <= _cur_level; i++)
    {
        Node<K, V>* node = _header->_forward[i];
        std::cout << "Level " << i << ": ";
        while (node != nullptr)
        {
            std::cout << node->key() << " : " << node->value() << ";";
            node = node->_forward[i];
        }
        std::cout << std::endl;
    }
}

template <typename K, typename V>
bool SkipList<K, V>::search_element(K key, V& value)
{
    Node<K, V>* cur = _header;
    // 小于向下走, 大于向右走
    for (int i = _cur_level; i >= 0; i--)
    {
        while (cur->_forward[i] && cur->_forward[i]->key() < key)
        {
            cur = cur->_forward[i];
        }
    }
    // 走到最后一层就把指针更新为右边的节点
    cur = cur->_forward[0];

    if (cur && cur->key() == key)
    {
        value = cur->value();
        std::cout << "Found Key: " << key << ", value: " << cur->value() << std::endl;
        return true;
    }
    std::cout << "Not Found Key: " << key << std::endl;
    return false;
}

template <typename K, typename V>
void SkipList<K, V>::delete_element(K key)
{
    _mutex.lock();
    // 和插入一样, 也需要在查找过程中记录下应该调整连接的节点
    Node<K, V>* cur = _header;
    Node<K, V>* update[_max_level + 1];
    memset(update, 0, sizeof(Node<K, V>*) * (_max_level + 1));

    for (int i = _cur_level; i >= 0; i--)
    {
        while (cur->_forward[i] != nullptr && cur->_forward[i]->key() < key)
        {
            cur = cur->_forward[i];
        }
        update[i] = cur;
    }

    cur = cur->_forward[0];
    if (cur != nullptr && cur->key() == key)
    {
        // 从当前节点的最底层开始删除
        for (int i = 0; i <= _cur_level; i++)
        {
            if (update[i]->_forward[i] != cur)
            {
                break;
            }
            update[i]->_forward[i] = cur->_forward[i];
        }
        // 将没有值的层移除
        while (_cur_level > 0 && _header->_forward[_cur_level] == 0)
        {
            _cur_level--;
        }
        std::cout << "The key: " << key << " was deleted successfully!!" << std::endl;
        delete cur;
        _node_count--;
    } 
    _mutex.unlock();
    return;
}

template <typename K, typename V>
void SkipList<K, V>::insert_set_element(K& key, V& value)
{
    V oldValue;
    if (search_element(key, oldValue))
    {
        delete_element(key);
    }
    insert_element(key, value);
}

template <typename K, typename V>
std::string SkipList<K, V>::dump_file()
{
    // _file_writer.open(STORE_FILE);
    Node<K, V>* node = _header->_forward[0];
    SkipListDump<K, V> dumper;
    while (node != nullptr)
    {
        dumper.insert(*node);
        // _file_writer << node->key() << ":" << node->value() << "\n";
        // std::cout << node->key() << ":" << node->value() << ";\n";
        node = node->_forward[0];
    }
    std::stringstream ss;
    boost::archive::text_oarchive oa(ss);
    oa << dumper;
    // _file_writer.flush();
    // _file_writer.close();
    return ss.str();
}

template <typename K, typename V>
void SkipList<K, V>::load_file(const std::string& dumpStr)
{
    if (dumpStr.empty())
    {
        return;
    }
    SkipListDump<K, V> dumper;
    std::stringstream iss(dumpStr);
    boost::archive::text_iarchive ia(iss);
    ia >> dumper;
    for (int i = 0; i < dumper._keyDumpVt.size(); i++)
    {
        insert_element(dumper._keyDumpVt[i], dumper._valueDumpVt[i]);
    }
}

template <typename K, typename V>
void SkipList<K, V>::clear(Node<K, V>* node)
{
    if (cur->_forward[0] != nullptr)
    {
        clear(cur->_forward[0]);
    }
    delete cur;
}

template <typename K, typename V>
int SkipList<K, V>::size()
{
    return _node_count;
}

template <typename K, typename V>
void SkipList<K, V>::get_key_value_from_string(const std::string& str, std::string* key, std::string* value)
{
    if (!is_valid_string(str))
    {
        return;
    }
    *key = str.substr(0, str.find(sep));
    *value = str.substr(str.find(sep) + 1, str.length());
}

template <typename K, typename V>
bool SkipList<K, V>::is_valid_string(const std::string& str)
{
    if (str.empty())
    {
        return false;
    }
    if (str.find(sep) == std::string::npos)
    {
        return false;
    }
    return true;
}

template <typename K, typename V>
void SkipListDump<K, V>::insert(const Node<K, V>& node) 
{
  keyDumpVt_.emplace_back(node.get_key());
  valDumpVt_.emplace_back(node.get_value());
}