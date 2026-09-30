// pm2 配置：在服务器上以 www 用户常驻运行中继服务
// 用法：pm2 start ecosystem.config.cjs && pm2 save
module.exports = {
    apps: [
        {
            name: 'ai-relay',
            script: 'src/index.js',
            cwd: '/www/wwwroot/api.class.feimsg.cn/app',
            node_args: '--env-file=/www/wwwroot/api.class.feimsg.cn/app/.env',
            instances: 1,
            exec_mode: 'fork',
            max_memory_restart: '300M',
            autorestart: true,
            out_file: '/www/wwwroot/api.class.feimsg.cn/app/logs/pm2-out.log',
            error_file: '/www/wwwroot/api.class.feimsg.cn/app/logs/pm2-err.log',
            time: true,
        },
    ],
};
