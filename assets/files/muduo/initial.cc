#include <stdio.h>
#include <vector>
#include <algorithm>
#include <unistd.h>
#include "../thread/Mutex.h"
#include "../thread/Thread.h"


class observable;

class observer{
    public:
        virtual ~observer();
        virtual void update() = 0;

        void observe_item(observable* s);

    protected:
        observable* subject_;
};

class observable{
    public:
        void register_(observer* x);
        void unregister(observer* x);
    
        void notify_observers()
        {
            for(size_t i = 0; i < observers_.size(); ++i)
            {
                observer* x = observers_[i];
                if(x){
                    x->update();
                }
            }
        }

    private:
        std::vector<observer*> observers_;
};

void observable::register_(observer* x){
    observers_.push_back(x);
}

void observable::unregister(observer* x){
    std::vector<observer*>::iterator it = std::find(observers_.begin(), observers_.end(), x);
    if(it != observers_.end())
    {
        std::swap(*it, observers_.back());
        observers_.pop_back();
    }
}

observer::~observer(){
    sleep(1);
    subject_->unregister(this);
}

void observer::observe_item(observable* s){
    s->register_(this);
    subject_ = s;
}

class foo : public observer{
    public:
        virtual void update()
        {
            printf("Foo::update() %p\n", this); 
        }
};

observable subject;
// thread B
void threadFunc()
{
    foo *p = new foo;
    p->observe_item(&subject);
    delete p;
}

int main(){
    muduo::Thread thread(threadFunc);
    thread.start();
    usleep(500 * 1000);
    subject.notify_observers();
}