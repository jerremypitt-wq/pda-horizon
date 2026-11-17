#include "horizon/HorizonGroupContacts.h"
#include <cassert>
#include <cstdint>

int main()
{
    HzGroupContacts g;
    const uint8_t key[] = {1, 2, 3};
    assert(!g.contains(100));
    g.configure(true, 1, "GROUP", 123, key, sizeof(key));
    assert(!g.observe(100, 0, true)); // общий канал не подтверждает членство
    assert(!g.observe(100, 1, false)); // зашифрованный/неразобранный пакет
    assert(!g.observe(0, 1, true)); // неверный узел
    assert(!g.contains(100));
    assert(g.observe(100, 1, true));
    assert(!g.observe(100, 1, true)); // повтор
    assert(g.contains(100));
    g.configure(true, 1, "GROUP", 123, key, sizeof(key));
    assert(g.contains(100)); // то же состояние сохраняет наблюдение
    g.configure(true, 2, "GROUP", 123, key, sizeof(key));
    assert(!g.contains(100)); // другой локальный слот
    assert(!g.observe(100, 1, true));
    assert(g.observe(200, 2, true));
    assert(g.contains(200));
    g.configure(true, 2, "OTHER", 123, key, sizeof(key));
    assert(!g.contains(200)); // другое имя
    g.observe(200, 2, true);
    g.configure(true, 2, "OTHER", 124, key, sizeof(key));
    assert(!g.contains(200)); // другой id канала
    g.observe(200, 2, true);
    const uint8_t newKey[] = {1, 2, 4};
    g.configure(true, 2, "OTHER", 124, newKey, sizeof(newKey));
    assert(!g.contains(200)); // другой ключ
    g.observe(200, 2, true);
    g.remove(200);
    assert(!g.contains(200));
    g.observe(200, 2, true);
    g.configure(false, 2, "OTHER", 124, newKey, sizeof(newKey));
    assert(!g.contains(200)); // канал отключён или неизвестен
    g.configure(true, 2, "OTHER", 124, newKey, sizeof(newKey));
    assert(!g.contains(200)); // не восстанавливать прошлую группировку
}
