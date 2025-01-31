#include "lock_manager.h"
#include "transaction_manager.h"
#include "../storage/Subsystem1_Manager.h"

void CheckGrowing(Transaction* txn) { assert(txn->getState() == TransactionState::GROWING); }

void CheckShrinking(Transaction* txn) { assert(txn->getState() == TransactionState::SHRINKING); }

void CheckAborted(Transaction* txn) { assert(txn->getState() == TransactionState::ABORTED); }

void CheckCommitted(Transaction* txn) { assert(txn->getState() == TransactionState::COMMITTED); }

void CheckTxnLockSize(Transaction* txn, size_t shared_size, size_t exclusive_size) {
    assert(txn->getSharedLockSet()->size() == shared_size);
    assert(txn->getExclusiveLockSet()->size() == exclusive_size);
}

// Basic shared lock test under REPEATABLE_READ
void BasicTest1() {
    LockManager lock_mgr;
    LogManager log_mgr("sys/Log/test");
    Subsystem1_Manager sm;
    TransactionManager txn_mgr(&lock_mgr, &log_mgr);

    std::vector<Trid> trids;
    std::vector<Transaction*> txns;
    int num_rids = 10;
    for (int i = 0; i < num_rids; i++) {
        //RID rid{ i, i };
        trids.push_back({"testRel" + to_string(i)});
        txns.push_back(txn_mgr.begin());
        assert(i == txns[i]->getTransactionId());
    }
    // test

    auto task = [&](int txn_id) {
        bool res;
        for (const Trid& trid : trids) {
            res = lock_mgr.LockShared(txns[txn_id], trid);
            assert(res == true);
            
            CheckGrowing(txns[txn_id]);
        }
        for (const Trid& trid : trids) {
            res = lock_mgr.Unlock(txns[txn_id], trid);
            assert(res == true);
            CheckShrinking(txns[txn_id]);
        }
        txn_mgr.commit(txns[txn_id]);
        CheckCommitted(txns[txn_id]);
    };
    std::vector<std::thread> threads;
    threads.reserve(num_rids);

    for (int i = 0; i < num_rids; i++) {
        threads.emplace_back(std::thread{ task, i });
    }

    for (int i = 0; i < num_rids; i++) {
        threads[i].join();
    }

    for (int i = 0; i < num_rids; i++) {
        delete txns[i];
    }
}

void BasicTest2() {
    LockManager lock_mgr;
    LogManager log_mgr("sys/Log/test");
    Subsystem1_Manager sm;
    TransactionManager txn_mgr(&lock_mgr, &log_mgr);

    std::vector<Trid> trids;
    Transaction* txn = txn_mgr.begin();
    int num_rids = 10;
    for (int i = 0; i < num_rids; i++) {
        //RID rid{ i, i };
        trids.push_back({ "testRel" + to_string(i)});
    }
    assert(0 == txn->getTransactionId());
    // test
    int txn_id = 0;
    bool res;
    for (const Trid& trid : trids) {
        res = lock_mgr.LockShared(txn, trid);
        assert(res == true);
        CheckGrowing(txn);
    }
    txn_mgr.commit(txn);
    CheckCommitted(txn);
}

void TwoPLTest() {
    LockManager lock_mgr;
    LogManager log_mgr("sys/Log/test");
    Subsystem1_Manager sm;
    TransactionManager txn_mgr(&lock_mgr, &log_mgr);
    //RID rid0{ 0, 0 };
    //RID rid1{ 0, 1 };
    Trid trid0{ "rel0" };
    Trid trid1{ "rel1" };

    auto txn = txn_mgr.begin();
    assert(0 == txn->getTransactionId());

    bool res;
    res = lock_mgr.LockShared(txn, trid0);
    assert(res == true);
    CheckGrowing(txn);
    CheckTxnLockSize(txn, 1, 0);

    res = lock_mgr.LockExclusive(txn, trid1);
    assert(res == true);
    CheckGrowing(txn);
    CheckTxnLockSize(txn, 1, 1);

    res = lock_mgr.Unlock(txn, trid0);
    assert(res == true);
    CheckShrinking(txn);
    CheckTxnLockSize(txn, 0, 1);

    lock_mgr.LockShared(txn, trid0);
    CheckAborted(txn);
    // Size shouldn't change here
    CheckTxnLockSize(txn, 0, 1);

    // Need to call txn_mgr's abort
    txn_mgr.abort(txn);
    CheckAborted(txn);
    CheckTxnLockSize(txn, 0, 0);

    delete txn;
}

void UpgradeTest() {
    LockManager lock_mgr;
    LogManager log_mgr("sys/Log/test");
    Subsystem1_Manager sm;
    TransactionManager txn_mgr(&lock_mgr, &log_mgr);
    //RID rid{ 0, 0 };
    Trid trid{ "rel" };
    Transaction txn(0);
    txn_mgr.begin(&txn);

    bool res = lock_mgr.LockShared(&txn, trid);
    assert(res == true);
    CheckTxnLockSize(&txn, 1, 0);
    CheckGrowing(&txn);

    res = lock_mgr.LockUpgrade(&txn, trid);
    assert(res == true);
    CheckTxnLockSize(&txn, 0, 1);
    CheckGrowing(&txn);

    res = lock_mgr.Unlock(&txn, trid);
    assert(res == true);
    CheckTxnLockSize(&txn, 0, 0);
    CheckShrinking(&txn);

    txn_mgr.commit(&txn);
    CheckCommitted(&txn);
}

// 本应排在后面的事务却先占了锁需要被ABORT处理
#include <future>
void WoundWaitBasicTest() {
    LockManager lock_mgr;
    LogManager log_mgr("sys/Log/test");
    Subsystem1_Manager sm;
    TransactionManager txn_mgr(&lock_mgr, &log_mgr);
    //RID rid{ 0, 0 };
    Trid trid{ "rel" };

    int id_hold = 0;
    int id_die = 1;

    std::promise<void> t1done;
    std::shared_future<void> t1_future(t1done.get_future());

    auto wait_die_task = [&]() {
        // younger transaction acquires lock first
        Transaction txn_die(id_die);
        txn_mgr.begin(&txn_die);
        bool res = lock_mgr.LockExclusive(&txn_die, trid);
        assert(res == true);

        CheckGrowing(&txn_die);
        CheckTxnLockSize(&txn_die, 0, 1);

        t1done.set_value();

        // wait for txn 0 to call lock_exclusive(), which should wound us
        std::this_thread::sleep_for(std::chrono::milliseconds(300));

        CheckAborted(&txn_die);

        // unlock
        txn_mgr.abort(&txn_die);
    };

    Transaction txn_hold(id_hold);
    txn_mgr.begin(&txn_hold);

    // launch the waiter thread
    std::thread wait_thread{ wait_die_task };

    // wait for txn1 to lock
    t1_future.wait();

    bool res = lock_mgr.LockExclusive(&txn_hold, trid);
    assert(res == true);

    wait_thread.join();

    CheckGrowing(&txn_hold);
    txn_mgr.commit(&txn_hold);
    CheckCommitted(&txn_hold);
}

// 测试没有死锁预防的情况，在加了wait-die算法逻辑后，t1在t0已经获取rid_a排它锁的情况下，试图获取rid_a的共享锁，将被abort
void DeadLockTest() {
    LockManager lock_mgr{ false };
    LogManager log_mgr("sys/Log/test");
    Subsystem1_Manager sm;
    TransactionManager txn_mgr(&lock_mgr, &log_mgr);
    //RID rid_a{ 0, 0 };
    Trid trid_a{ "rela" };
    int account_a = 100;
    //RID rid_b{ 0, 1 };
    Trid trid_b{ "relb" };
    int account_b = 200;

    // 可能出现死锁
    std::thread t0([&](int account_a, int account_b) {
        Transaction txn(0);

        bool res;
        res = lock_mgr.LockExclusive(&txn, trid_b);
        //LOG_DEBUG("thread 0 lock rid_b");
        assert(res == true);
        assert(txn.getState() == TransactionState::GROWING);
        account_b -= 50;

        std::this_thread::sleep_for(std::chrono::milliseconds(1000));

        res = lock_mgr.LockExclusive(&txn, trid_a);
        //LOG_DEBUG("thread 0 lock rid_a");
        assert(res, true);
        assert(txn.getState() == TransactionState::GROWING);
        account_a += 50;

        lock_mgr.Unlock(&txn, trid_a);
        //LOG_DEBUG("thread 0 unlock rid_a");
        assert(txn.getState() == TransactionState::SHRINKING);
        lock_mgr.Unlock(&txn, trid_b);
        //LOG_DEBUG("thread 0 unlock rid_b");
        assert(txn.getState() == TransactionState::SHRINKING);
        }, account_a, account_b);

    std::thread t1([&] {
        Transaction txn(1);
        bool res = lock_mgr.LockShared(&txn, trid_a);
        //LOG_DEBUG("thread 1 lock rid_a");
        assert(res == true);
        assert(txn.getState() == TransactionState::GROWING);

        std::this_thread::sleep_for(std::chrono::milliseconds(1000));

        res = lock_mgr.LockShared(&txn, trid_b);
        assert(res == false);
        assert(txn.getState() == TransactionState::ABORTED);
        txn_mgr.abort(&txn);
        });

    t0.join();
    t1.join();
}

//int main(void) {
// 
//    // 最基本测试
//    BasicTest1();
//    BasicTest2();
// 
//    // 两阶段基本测试
//    TwoPLTest();
// 
//    // 锁升级基本测试
//    UpgradeTest();
// 
//    // 顺序逻辑错误基本测试
//    WoundWaitBasicTest();
// 
//    // 死锁避免基本测试
//    DeadLockTest();
//
//    return 0;
//}