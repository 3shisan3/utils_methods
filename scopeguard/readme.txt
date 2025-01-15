用途：
    防止异常终止，提前返回导致的资源未完全释放问题

使用方法：
    调用对应宏后接执行的代码块直接进行使用,或生成具体实例，自行操作使用
    继承方法额外拓展（加入用于不同场景的执行函数标签；执行函数中包含异常抛出时需自定义异常场景时的打印等函数重写）

来源：（迭代后版本）
    参考https://github.com/facebook/folly/blob/main/folly/ScopeGuard.h 的实现
    更多增加继承提供的scopeguard类的拓展相关服务完善
    /* folly库中scopeguard分得更细，借由uncaught_exceptions的数量差异来判断，引入标记：
            scope success即scopeguard所在函数正常退出未抛出异常场景会执行 
            scope fail 为抛出异常导致函数退出场景会执行
            scope exit 无论如何都会执行
    */